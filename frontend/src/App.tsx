import { FormEvent, useCallback, useEffect, useRef, useState } from "react";
import { Activity, FileText, Folder, Search, ShieldCheck, RotateCcw } from "lucide-react";

type SearchResult = {
  path: string;
  file_name: string;
  extension?: string;
  size_bytes: number;
  modified_at: number;
  content?: string;
};
type SearchSchema = { keywords: string[]; extensions: string[]; intent: string };
type Snapshot = SearchResult & { version: number };
type DirectoryHandle = FileSystemDirectoryHandle & { values(): AsyncIterableIterator<FileSystemFileHandle | FileSystemDirectoryHandle> };

declare global {
  interface Window {
    showDirectoryPicker?: () => Promise<DirectoryHandle>;
    __TAURI__?: { invoke: (command: string, args: unknown) => Promise<SearchResult[]> };
  }
}

const ignoredWords = new Set(["find", "the", "from", "last", "that", "mention", "mentions", "files", "file", "recent"]);
const textExtensions = new Set(["json", "md", "csv", "log", "rs", "py", "js", "ts", "tsx", "txt", "html", "css"]);

async function collectFiles(directory: DirectoryHandle, prefix = ""): Promise<Array<{ handle: FileSystemFileHandle; path: string }>> {
  const files: Array<{ handle: FileSystemFileHandle; path: string }> = [];
  for await (const entry of directory.values()) {
    const path = prefix ? `${prefix}/${entry.name}` : entry.name;
    if (entry.kind === "file") files.push({ handle: entry, path });
    else files.push(...await collectFiles(entry as DirectoryHandle, path));
  }
  return files;
}

async function snapshotFiles(directory: DirectoryHandle): Promise<SearchResult[]> {
  const files = await collectFiles(directory);
  const snapshots: SearchResult[] = [];
  for (const item of files) {
    const file = await item.handle.getFile();
    const extension = file.name.includes(".") ? file.name.split(".").pop()?.toLowerCase() : undefined;
    const content = extension && textExtensions.has(extension) ? await file.slice(0, 256 * 1024).text() : undefined;
    snapshots.push({ path: item.path, file_name: file.name, extension, size_bytes: file.size, modified_at: file.lastModified, content });
  }
  return snapshots;
}

async function parseWithBackend(query: string): Promise<SearchSchema | undefined> {
  try {
    const response = await fetch("http://127.0.0.1:8787/api/parse", { method: "POST", headers: { "Content-Type": "application/json" }, body: JSON.stringify({ query }) });
    if (!response.ok) throw new Error(`AI backend returned ${response.status}. Check the Rust server and Groq key.`);
    return await response.json() as SearchSchema;
  } catch (error) {
    if (error instanceof Error && error.message.startsWith("AI backend")) throw error;
    return undefined;
  }
}

function matchesQuery(file: SearchResult, query: string, schema?: SearchSchema) {
  const extension = schema?.extensions[0]?.replace(/^\./, "").toLowerCase() ?? query.match(/\b(?:pdf|docx?|xlsx?|txt|md|rs|py|js|tsx?)\b/i)?.[0].toLowerCase();
  const keywords = schema?.keywords ?? query.toLowerCase().split(/[^a-z0-9]+/).filter((word) => word.length > 2 && !ignoredWords.has(word) && word !== extension);
  if (extension && file.extension !== extension) return false;
  const searchable = `${file.path} ${file.content ?? ""}`.toLowerCase();
  return !keywords.length || keywords.some((keyword) => searchable.includes(keyword.toLowerCase()));
}

function ResultCard({ result }: { result: SearchResult }) {
  return <article className="result-card"><div className="file-icon"><FileText size={22} /></div><div><h3>{result.file_name}</h3><p>{result.path}</p><small>{result.extension?.toUpperCase() ?? "FILE"} · {Math.round(result.size_bytes / 1000)} KB</small></div></article>;
}

function FolderPrompt({ chooseFolder }: { chooseFolder: () => void }) {
  return <div className="empty-state"><Folder size={30} /><h2>Choose a folder to begin</h2><p>SafeVault needs permission to watch your local files.</p><button onClick={chooseFolder}>Choose folder</button></div>;
}

export function App() {
  const [activeView, setActiveView] = useState<"search" | "activity" | "timeline">("search");
  const [query, setQuery] = useState("");
  const [root, setRoot] = useState<DirectoryHandle | null>(null);
  const [rootName, setRootName] = useState("");
  const [results, setResults] = useState<SearchResult[]>([]);
  const [history, setHistory] = useState<Snapshot[]>([]);
  const [selectedPath, setSelectedPath] = useState("");
  const [isSearching, setIsSearching] = useState(false);
  const [error, setError] = useState("");
  const previousRef = useRef<Map<string, SearchResult>>(new Map());

  const refresh = useCallback(async (directory: DirectoryHandle, recordChanges: boolean) => {
    const current = await snapshotFiles(directory);
    const previous = previousRef.current;
    if (recordChanges) {
      const changed = current.filter((file) => {
        const old = previous.get(file.path);
        return old && (old.modified_at !== file.modified_at || old.size_bytes !== file.size_bytes || old.content !== file.content);
      });
      if (changed.length) setHistory((old) => [...old, ...changed.map((file) => ({ ...previous.get(file.path)!, version: Date.now() }))]);
    }
    previousRef.current = new Map(current.map((file) => [file.path, file]));
    setResults(current);
  }, []);

  async function chooseFolder() {
    setError("");
    if (!window.showDirectoryPicker) { setError("Folder access requires Chrome or Edge, or the Tauri desktop app."); return; }
    try {
      const selected = await window.showDirectoryPicker();
      setRoot(selected); setRootName(selected.name); setHistory([]); previousRef.current.clear();
      setIsSearching(true); await refresh(selected, false); setIsSearching(false);
    } catch (cause) {
      setIsSearching(false);
      if ((cause as DOMException).name !== "AbortError") setError("Could not access that folder.");
    }
  }

  useEffect(() => {
    if (!root) return;
    const timer = window.setInterval(() => void refresh(root, true).catch(() => setError("Folder access was lost. Choose the folder again.")), 2000);
    return () => window.clearInterval(timer);
  }, [root, refresh]);

  useEffect(() => {
    if (!rootName) return;
    const key = `safevault-history:${rootName}`;
    try {
      const saved = localStorage.getItem(key);
      if (saved) setHistory(JSON.parse(saved) as Snapshot[]);
    } catch {
      setError("Saved history could not be loaded.");
    }
  }, [rootName]);

  useEffect(() => {
    if (!rootName || !history.length) return;
    localStorage.setItem(`safevault-history:${rootName}`, JSON.stringify(history.slice(-100)));
  }, [history, rootName]);

  async function submit(event: FormEvent) {
    event.preventDefault();
    if (!root) { setError("Choose a folder before searching."); return; }
    setError(""); setIsSearching(true);
    try {
      if (window.__TAURI__) setResults(await window.__TAURI__.invoke("search_files", { query: query.trim(), root: rootName }));
      else {
        const schema = await parseWithBackend(query.trim());
        setResults((await snapshotFiles(root)).filter((file) => matchesQuery(file, query.trim(), schema)));
      }
    } catch (cause) { setError(cause instanceof Error ? cause.message : "Search failed."); }
    finally { setIsSearching(false); }
  }

  async function restore(snapshot: Snapshot) {
    if (!root || snapshot.content === undefined) { setError("Only text-file snapshots can be restored in browser mode."); return; }
    try {
      const parts = snapshot.path.split("/");
      let directory = root;
      for (const part of parts.slice(0, -1)) directory = await directory.getDirectoryHandle(part) as DirectoryHandle;
      const handle = await directory.getFileHandle(parts[parts.length - 1]);
      const writable = await handle.createWritable();
      await writable.write(snapshot.content); await writable.close();
      await refresh(root, false);
    } catch { setError("Could not restore this version. Check folder write permission."); }
  }

  const selectedHistory = history.filter((item) => item.path === selectedPath).sort((a, b) => b.version - a.version);
  const timeline = [...results].sort((a, b) => b.modified_at - a.modified_at);
  return <main className="shell"><aside className="sidebar">
    <div className="brand"><span className="brand-mark">S</span><span>SafeVault</span></div>
    <nav><button className={activeView === "search" ? "nav-item active" : "nav-item"} onClick={() => setActiveView("search")}><Search size={17} /> AI Search</button><button className={activeView === "activity" ? "nav-item active" : "nav-item"} onClick={() => setActiveView("activity")}><Activity size={17} /> Activity journal</button><button className={activeView === "timeline" ? "nav-item active" : "nav-item"} onClick={() => setActiveView("timeline")}><Folder size={17} /> File timeline</button></nav>
    <div className="protection"><ShieldCheck size={18} /><div><strong>{root ? "Monitoring active" : "Protection idle"}</strong><small>{root ? "Polling every 2 seconds" : "Choose a folder"}</small></div></div>
  </aside><section className="content">
    {activeView === "search" && <><header><div><p className="eyebrow">SAFEVAULT FS / AI SEARCH</p><h1>Find anything in your vault.</h1><p className="subtitle">Search your actual files by name, type, or content.</p></div><span className="status-dot">● Online</span></header><div className="folder-bar"><Folder size={18} /><span>{rootName || "No folder selected"}</span><button type="button" onClick={chooseFolder}>{root ? "Change folder" : "Choose folder"}</button></div><form className="search-box" onSubmit={submit}><Search size={21} /><input value={query} onChange={(event) => setQuery(event.target.value)} placeholder="Find PDF invoices mentioning AWS..." /><button type="submit" disabled={isSearching}>{isSearching ? "Searching..." : "Search"}</button></form>{error && <p className="error">{error}</p>}<div className="results-head"><div><p className="eyebrow">SEARCH RESULTS</p><h2>{results.length} files found</h2></div><span className="streaming">{isSearching ? "● Scanning..." : root ? "● Live index" : "● Select a folder"}</span></div><div className="result-grid">{results.map((result) => <ResultCard key={result.path} result={result} />)}</div></>}
    {activeView === "activity" && <><header><div><p className="eyebrow">SAFEVAULT FS / ACTIVITY JOURNAL</p><h1>Live file activity.</h1><p className="subtitle">Changes detected automatically every two seconds.</p></div><span className="status-dot">● {root ? "Watching" : "Idle"}</span></header>{results.length ? <div className="event-list">{timeline.map((result) => <div className="event-row" key={result.path}><Activity size={18} /><div><strong>{result.file_name}</strong><p>{result.path}</p></div><time>{new Date(result.modified_at).toLocaleString()}</time></div>)}</div> : <FolderPrompt chooseFolder={chooseFolder} />}</>}
    {activeView === "timeline" && <><header><div><p className="eyebrow">SAFEVAULT FS / FILE TIMELINE</p><h1>Previous versions.</h1><p className="subtitle">Select a changed file to inspect its saved snapshots.</p></div><span className="status-dot">● {history.length} snapshots</span></header>{timeline.length ? <div className="timeline">{timeline.map((result) => <button className="timeline-row" key={result.path} onClick={() => setSelectedPath(result.path)}><time>{new Date(result.modified_at).toLocaleString()}</time><span className="timeline-line" /><ResultCard result={result} /></button>)}</div> : <FolderPrompt chooseFolder={chooseFolder} />}{selectedPath && <div className="history-panel"><h2>History: {selectedPath}</h2>{selectedHistory.length ? selectedHistory.map((snapshot) => <div className="history-row" key={snapshot.version}><span>{new Date(snapshot.version).toLocaleString()}</span><span>{snapshot.size_bytes} bytes</span><button onClick={() => void restore(snapshot)}><RotateCcw size={14} /> Restore</button></div>) : <p className="subtitle">No changes captured yet. Edit this file while the folder is open.</p>}</div>}</>}
    {error && activeView !== "search" && <p className="error">{error}</p>}
  </section></main>;
}
