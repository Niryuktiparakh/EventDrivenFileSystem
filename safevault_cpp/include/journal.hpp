#pragma once
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <utility>
#include <vector>

struct sqlite3;

// ─────────────────────────────────────────────
// EventKind  (mirrors Rust enum EventKind)
// ─────────────────────────────────────────────
enum class EventKind { Create, Modify, Delete, Rename };

std::string event_kind_str(EventKind k);   // "CREATE" / "MODIFY" / …
EventKind   event_kind_from_str(const std::string& s);

// ─────────────────────────────────────────────
// EventRecord  (one row in the events table)
// ─────────────────────────────────────────────
struct EventRecord {
    int64_t                                 id{0};
    std::string                             kind;
    std::filesystem::path                   path;
    std::optional<std::filesystem::path>    old_path;
    std::string                             timestamp; // RFC-3339 UTC
    uint32_t                                pid{0};

    // Factory: capture current time + process id
    static EventRecord now(EventKind k,
                           std::filesystem::path path,
                           std::optional<std::filesystem::path> old_path,
                           uint32_t pid);
};

// ─────────────────────────────────────────────
// VersionRecord  (metadata row from file_versions)
// ─────────────────────────────────────────────
struct VersionRecord {
    int64_t               id{0};
    std::filesystem::path path;
    std::string           created_at;
    size_t                size_bytes{0};
};

// ─────────────────────────────────────────────
// Journal  (owns the SQLite connection)
// ─────────────────────────────────────────────
class Journal {
public:
    explicit Journal(const std::filesystem::path& db_path);
    ~Journal();

    // Disallow copy; allow move
    Journal(const Journal&)            = delete;
    Journal& operator=(const Journal&) = delete;
    Journal(Journal&&)                 = default;
    Journal& operator=(Journal&&)      = default;

    // Append a new event row; returns the new rowid
    int64_t append(const EventRecord& ev);

    // Query event history. If path is empty, returns all events.
    std::vector<EventRecord> history(
        std::optional<std::filesystem::path> path = std::nullopt) const;

    // Events whose path starts with root (recursive)
    std::vector<EventRecord> history_under(
        const std::filesystem::path& root) const;

    // Save a file snapshot before modification/deletion
    int64_t save_version(const std::filesystem::path& path,
                         const std::vector<uint8_t>& content);

    // List saved versions for a file (newest first)
    std::vector<VersionRecord> versions(
        const std::filesystem::path& path) const;

    // Retrieve path + raw bytes for a specific version id
    std::pair<std::filesystem::path, std::vector<uint8_t>>
    version_content(int64_t version_id) const;

private:
    sqlite3* db_{nullptr};

    void exec(const char* sql) const;
};
