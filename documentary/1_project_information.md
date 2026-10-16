# 1. SafeVault FS Project Information

## Project name

**SafeVault FS** is an operating-system-level, AI-assisted journaling
filesystem project. It is designed to record filesystem activity, preserve
previous file states, support rollback, and provide natural-language search.

The project was previously referred to as ChronoFS. The current product name
is SafeVault FS.

## Problem statement

Normal filesystems usually expose only the current state of a file. If a user
accidentally overwrites a document, a destructive script removes files, or
malware encrypts data, recovery may be difficult or impossible without a
separate backup.

SafeVault FS addresses this problem by maintaining a history of filesystem
operations and previous file contents. The goal is to make recovery automatic
and easy to inspect.

## Main objectives

1. Monitor file operations continuously.
2. Record create, modify, delete, and rename activity.
3. Attribute operations to process information such as PID and executable
   path.
4. Preserve previous file versions for rollback.
5. Search files using natural-language queries.
6. Provide a terminal menu and a future desktop interface.
7. Add security features such as entropy analysis and rate limiting.

## Current implementation

The repository currently contains two implementation tracks:

- A preserved Python watchdog prototype.
- A Rust implementation track in the `safevault` directory.

The Rust implementation currently provides:

- SQLite event journaling with WAL mode.
- A continuous filesystem watcher.
- A terminal menu for activity and history.
- Saved previous file contents for rollback.
- A Rust search engine using `jwalk` and `rayon`.
- Optional Groq query parsing through a local Rust API.
- A React frontend for folder selection, AI search, activity, and timeline
  views.

## Planned architecture

```text
User applications
        |
        v
SafeVault mount / filesystem layer
        |
        +--> Event journal
        +--> Version and chunk storage
        +--> Threat detection
        +--> Search engine
        |
        v
SQLite metadata + hidden SafeVault storage
        |
        v
Rust API / Tauri desktop frontend
```

## Planned technology stack

| Layer | Technology |
|---|---|
| Filesystem layer | Rust, FUSE or a platform-specific filesystem API |
| Journal | SQLite with WAL |
| Chunking | FastCDC or Rabin-style content-defined chunking |
| Hashing | BLAKE3 or SHA-256 |
| Compression | Zstandard |
| Concurrency | Tokio, rayon, jwalk |
| AI parsing | Groq API with strict JSON output |
| Desktop UI | Tauri, React, TypeScript |
| Security | Entropy analysis, rate limiting, process attribution |

## Project status

The current work is focused on the OS foundation and an early search/UI
vertical slice. Full transparent FUSE mounting, chunk deduplication,
ransomware blocking, and production Tauri packaging remain future milestones.
