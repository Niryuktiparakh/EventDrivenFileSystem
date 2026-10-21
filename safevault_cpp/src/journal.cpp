#include "journal.hpp"
#include <sqlite3.h>

#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <stdexcept>

#ifdef _WIN32
#  include <windows.h>
#else
#  include <unistd.h>
#endif

// ─────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────

static std::string utc_now_rfc3339() {
    auto now = std::chrono::system_clock::now();
    auto tt  = std::chrono::system_clock::to_time_t(now);
    std::tm tm_utc{};
#ifdef _WIN32
    gmtime_s(&tm_utc, &tt);
#else
    gmtime_r(&tt, &tm_utc);
#endif
    std::ostringstream oss;
    oss << std::put_time(&tm_utc, "%Y-%m-%dT%H:%M:%SZ");
    return oss.str();
}

static uint32_t current_pid() {
#ifdef _WIN32
    return static_cast<uint32_t>(GetCurrentProcessId());
#else
    return static_cast<uint32_t>(getpid());
#endif
}

// ─────────────────────────────────────────────
// EventKind helpers
// ─────────────────────────────────────────────

std::string event_kind_str(EventKind k) {
    switch (k) {
        case EventKind::Create: return "CREATE";
        case EventKind::Modify: return "MODIFY";
        case EventKind::Delete: return "DELETE";
        case EventKind::Rename: return "RENAME";
    }
    return "UNKNOWN";
}

EventKind event_kind_from_str(const std::string& s) {
    if (s == "CREATE") return EventKind::Create;
    if (s == "MODIFY") return EventKind::Modify;
    if (s == "DELETE") return EventKind::Delete;
    if (s == "RENAME") return EventKind::Rename;
    throw std::runtime_error("Unknown event kind: " + s);
}

// ─────────────────────────────────────────────
// EventRecord::now
// ─────────────────────────────────────────────

EventRecord EventRecord::now(EventKind k,
                              std::filesystem::path p,
                              std::optional<std::filesystem::path> old_p,
                              uint32_t pid) {
    EventRecord r;
    r.id        = 0;
    r.kind      = event_kind_str(k);
    r.path      = std::move(p);
    r.old_path  = std::move(old_p);
    r.timestamp = utc_now_rfc3339();
    r.pid       = pid;
    return r;
}

// ─────────────────────────────────────────────
// RAII statement wrapper
// ─────────────────────────────────────────────

struct Stmt {
    sqlite3_stmt* s{nullptr};
    explicit Stmt(sqlite3* db, const char* sql) {
        if (sqlite3_prepare_v2(db, sql, -1, &s, nullptr) != SQLITE_OK)
            throw std::runtime_error(std::string("SQLite prepare: ") + sqlite3_errmsg(db));
    }
    ~Stmt() { if (s) sqlite3_finalize(s); }
    Stmt(const Stmt&)            = delete;
    Stmt& operator=(const Stmt&) = delete;
};

// ─────────────────────────────────────────────
// Journal
// ─────────────────────────────────────────────

Journal::Journal(const std::filesystem::path& db_path) {
    // Ensure parent directory exists
    std::filesystem::create_directories(db_path.parent_path());

    if (sqlite3_open_v2(db_path.string().c_str(), &db_,
                        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX,
                        nullptr) != SQLITE_OK) {
        throw std::runtime_error(std::string("SQLite open: ") + sqlite3_errmsg(db_));
    }

    exec("PRAGMA journal_mode=WAL;");
    exec("PRAGMA foreign_keys=ON;");
    exec(R"(
        CREATE TABLE IF NOT EXISTS events (
            id           INTEGER PRIMARY KEY AUTOINCREMENT,
            event_type   TEXT    NOT NULL,
            path         TEXT    NOT NULL,
            old_path     TEXT,
            timestamp    TEXT    NOT NULL,
            pid          INTEGER NOT NULL,
            parent_pid   INTEGER,
            process_path TEXT
        );
        CREATE INDEX IF NOT EXISTS idx_events_path_timestamp
            ON events(path, timestamp);
        CREATE TABLE IF NOT EXISTS file_versions (
            id         INTEGER PRIMARY KEY AUTOINCREMENT,
            path       TEXT    NOT NULL,
            created_at TEXT    NOT NULL,
            content    BLOB    NOT NULL
        );
        CREATE INDEX IF NOT EXISTS idx_versions_path_created
            ON file_versions(path, created_at);
    )");
}

Journal::~Journal() {
    if (db_) sqlite3_close(db_);
}

void Journal::exec(const char* sql) const {
    char* errmsg = nullptr;
    if (sqlite3_exec(db_, sql, nullptr, nullptr, &errmsg) != SQLITE_OK) {
        std::string msg = errmsg ? errmsg : "unknown error";
        sqlite3_free(errmsg);
        throw std::runtime_error("SQLite exec: " + msg);
    }
}

int64_t Journal::append(const EventRecord& ev) {
    Stmt st(db_,
        "INSERT INTO events (event_type, path, old_path, timestamp, pid, parent_pid, process_path) "
        "VALUES (?1, ?2, ?3, ?4, ?5, NULL, NULL)");

    sqlite3_bind_text(st.s, 1, ev.kind.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st.s, 2, ev.path.string().c_str(), -1, SQLITE_TRANSIENT);
    if (ev.old_path)
        sqlite3_bind_text(st.s, 3, ev.old_path->string().c_str(), -1, SQLITE_TRANSIENT);
    else
        sqlite3_bind_null(st.s, 3);
    sqlite3_bind_text(st.s, 4, ev.timestamp.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(st.s, 5, static_cast<sqlite3_int64>(ev.pid));

    if (sqlite3_step(st.s) != SQLITE_DONE)
        throw std::runtime_error(std::string("SQLite insert event: ") + sqlite3_errmsg(db_));

    return sqlite3_last_insert_rowid(db_);
}

std::vector<EventRecord> Journal::history(
    std::optional<std::filesystem::path> path) const
{
    const char* sql =
        "SELECT id, event_type, path, old_path, timestamp, pid "
        "FROM events "
        "WHERE (?1 IS NULL OR path = ?1) "
        "ORDER BY id ASC";

    Stmt st(db_, sql);
    if (path)
        sqlite3_bind_text(st.s, 1, path->string().c_str(), -1, SQLITE_TRANSIENT);
    else
        sqlite3_bind_null(st.s, 1);

    std::vector<EventRecord> rows;
    while (sqlite3_step(st.s) == SQLITE_ROW) {
        EventRecord r;
        r.id        = sqlite3_column_int64(st.s, 0);
        r.kind      = reinterpret_cast<const char*>(sqlite3_column_text(st.s, 1));
        r.path      = reinterpret_cast<const char*>(sqlite3_column_text(st.s, 2));
        auto* op    = sqlite3_column_text(st.s, 3);
        if (op) r.old_path = std::filesystem::path(reinterpret_cast<const char*>(op));
        r.timestamp = reinterpret_cast<const char*>(sqlite3_column_text(st.s, 4));
        r.pid       = static_cast<uint32_t>(sqlite3_column_int(st.s, 5));
        rows.push_back(std::move(r));
    }
    return rows;
}

std::vector<EventRecord> Journal::history_under(
    const std::filesystem::path& root) const
{
    // Normalise: remove trailing separators
    std::string root_str = root.string();
    while (!root_str.empty() &&
           (root_str.back() == '\\' || root_str.back() == '/'))
        root_str.pop_back();

    const char* sql =
        "SELECT id, event_type, path, old_path, timestamp, pid "
        "FROM events "
        "WHERE path = ?1 OR path LIKE ?2 OR path LIKE ?3 "
        "ORDER BY id ASC";

    Stmt st(db_, sql);
    sqlite3_bind_text(st.s, 1, root_str.c_str(), -1, SQLITE_TRANSIENT);
    std::string like_win = root_str + "\\%";
    std::string like_nix = root_str + "/%";
    sqlite3_bind_text(st.s, 2, like_win.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st.s, 3, like_nix.c_str(), -1, SQLITE_TRANSIENT);

    std::vector<EventRecord> rows;
    while (sqlite3_step(st.s) == SQLITE_ROW) {
        EventRecord r;
        r.id        = sqlite3_column_int64(st.s, 0);
        r.kind      = reinterpret_cast<const char*>(sqlite3_column_text(st.s, 1));
        r.path      = reinterpret_cast<const char*>(sqlite3_column_text(st.s, 2));
        auto* op    = sqlite3_column_text(st.s, 3);
        if (op) r.old_path = std::filesystem::path(reinterpret_cast<const char*>(op));
        r.timestamp = reinterpret_cast<const char*>(sqlite3_column_text(st.s, 4));
        r.pid       = static_cast<uint32_t>(sqlite3_column_int(st.s, 5));
        rows.push_back(std::move(r));
    }
    return rows;
}

int64_t Journal::save_version(const std::filesystem::path& path,
                               const std::vector<uint8_t>& content) {
    Stmt st(db_,
        "INSERT INTO file_versions (path, created_at, content) "
        "VALUES (?1, ?2, ?3)");

    std::string ts = utc_now_rfc3339();
    sqlite3_bind_text(st.s, 1, path.string().c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st.s, 2, ts.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_blob(st.s, 3,
                      content.empty() ? nullptr : content.data(),
                      static_cast<int>(content.size()),
                      SQLITE_TRANSIENT);

    if (sqlite3_step(st.s) != SQLITE_DONE)
        throw std::runtime_error(std::string("SQLite insert version: ") + sqlite3_errmsg(db_));

    return sqlite3_last_insert_rowid(db_);
}

std::vector<VersionRecord> Journal::versions(
    const std::filesystem::path& path) const
{
    Stmt st(db_,
        "SELECT id, path, created_at, length(content) "
        "FROM file_versions WHERE path = ?1 ORDER BY id DESC");

    sqlite3_bind_text(st.s, 1, path.string().c_str(), -1, SQLITE_TRANSIENT);

    std::vector<VersionRecord> rows;
    while (sqlite3_step(st.s) == SQLITE_ROW) {
        VersionRecord r;
        r.id         = sqlite3_column_int64(st.s, 0);
        r.path       = reinterpret_cast<const char*>(sqlite3_column_text(st.s, 1));
        r.created_at = reinterpret_cast<const char*>(sqlite3_column_text(st.s, 2));
        r.size_bytes = static_cast<size_t>(sqlite3_column_int64(st.s, 3));
        rows.push_back(std::move(r));
    }
    return rows;
}

std::pair<std::filesystem::path, std::vector<uint8_t>>
Journal::version_content(int64_t version_id) const {
    Stmt st(db_,
        "SELECT path, content FROM file_versions WHERE id = ?1");
    sqlite3_bind_int64(st.s, 1, version_id);

    if (sqlite3_step(st.s) != SQLITE_ROW)
        throw std::runtime_error("Version not found: " + std::to_string(version_id));

    std::filesystem::path p =
        reinterpret_cast<const char*>(sqlite3_column_text(st.s, 0));
    const auto* blob = static_cast<const uint8_t*>(sqlite3_column_blob(st.s, 1));
    int sz = sqlite3_column_bytes(st.s, 1);
    std::vector<uint8_t> data(blob, blob + sz);
    return {p, std::move(data)};
}
