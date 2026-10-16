# SafeVault FS

SafeVault FS is an AI-driven journaling filesystem project. Development is
split into two tracks while the Rust implementation is built:

- [`safevault/`](./safevault/) contains the new Rust implementation.
- [`frontend/`](./frontend/) contains the React search interface.
- [`engine/`](./engine/) and [`journal/`](./journal/) contain the preserved
  Python watchdog prototype.

## Current milestone

The Rust Phase 1 foundation provides a SQLite WAL event journal and a CLI for
recording and inspecting filesystem events. The AI search slice now provides
natural-language query parsing, optional Groq schema extraction, concurrent
filesystem traversal, and a React interface ready for Tauri IPC.

See [`safevault/README.md`](./safevault/README.md) for Rust setup and usage.

## Run the frontend with real files

```powershell
cd "D:\3rd_year\OS\EventDrivenFileSystem\frontend"
npm install
npm run dev
```

For AI parsing in the browser, start the local Rust API in a second terminal:

```powershell
cd "D:\3rd_year\OS\EventDrivenFileSystem\safevault"
cargo run -- serve
```

Open the local Vite URL in Chrome or Edge, click **Choose folder**, and select
the folder SafeVault should search. The browser scans that selected folder
locally; it does not use dummy files. Text-like files are searched by both
filename and the first 64 KB of content. Binary files are searched by filename
and extension. A future Tauri build will replace the browser folder picker
with the Rust backend and unrestricted app-managed vault access.

Never put `GROQ_API_KEY` in frontend code or expose it through Vite. Keep it in
the ignored root `.env` for the Rust backend, and rotate any key that has been
shared or exposed.

## Continuous tracking

The browser watcher only runs while its page is open. For reliable background
tracking, run the Rust watcher:

```powershell
cd "D:\3rd_year\OS\EventDrivenFileSystem\safevault"
cargo run -- watch "D:\path\to\folder"
```

This process watches recursively, journals changes immediately, and opens a
terminal menu for activity and history. It does not require the frontend to be
open or reloaded.