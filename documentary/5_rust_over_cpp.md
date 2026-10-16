# 5. Why Rust Instead of C++?

## 5.1 Memory safety

Filesystem software handles paths, buffers, metadata, concurrency, and
long-running processes. Memory errors in this layer can cause crashes,
corrupted state, or security vulnerabilities.

Rust prevents many classes of bugs at compile time:

- Use-after-free.
- Double-free.
- Null-like invalid ownership.
- Data races between threads.
- Dangling references.

C++ can also be written safely, but safety depends more heavily on coding
discipline, reviews, static analysis, and careful library use.

## 5.2 Ownership fits filesystem resources

Rust ownership makes resource lifetime explicit. This is useful for:

- SQLite connections.
- File handles.
- Watcher handles.
- Memory buffers.
- Temporary storage.
- Network clients.

When an owner goes out of scope, Rust can automatically release the resource
through deterministic destruction.

## 5.3 Concurrency safety

SafeVault must monitor files while processing events and serving searches.
Rust's `Send` and `Sync` rules prevent many unsafe patterns before the program
runs.

This is valuable for:

- Event channels.
- Parallel directory scans.
- Background watchers.
- Streaming search results.
- Shared journal access.

C++ supports threads and advanced concurrency libraries, but it does not
provide the same compile-time data-race guarantees by default.

## 5.4 Performance

Rust produces native binaries with performance close to C and C++. It has:

- No garbage collector.
- Predictable resource management.
- Zero-cost abstractions in many common cases.
- Efficient control over allocation and I/O.

This is appropriate for a filesystem service that should remain responsive
under high file activity.

## 5.5 Strong type system

Rust's enums and pattern matching make event types explicit:

```rust
enum EventKind {
    Create,
    Modify,
    Delete,
    Rename,
}
```

This reduces accidental handling omissions and makes state transitions easier
to review.

## 5.6 Error handling

Rust encourages explicit `Result`-based error handling. Filesystem operations
can fail for many legitimate reasons, so errors should be propagated or
handled deliberately.

The project uses `anyhow` for application-level error context and should use
more specific error types as the architecture grows.

## 5.7 Modern ecosystem

The SafeVault implementation benefits from Rust libraries such as:

- `rusqlite` for SQLite.
- `notify` for filesystem events.
- `jwalk` for directory traversal.
- `rayon` for parallel work.
- `reqwest` for Groq communication.
- `axum` for the local API.
- `serde` for JSON schemas.
- `clap` for the terminal interface.

These libraries provide a consistent ecosystem with strong compiler
integration.

## 5.8 Smaller security risk surface

Because memory safety and thread-safety issues are checked during compilation,
Rust reduces several common vulnerability categories. It does not eliminate
all security problems: path traversal, authorization errors, logic bugs,
cryptographic mistakes, and denial-of-service problems still require careful
design.

## 5.9 Why not C++?

C++ would also be technically capable of implementing SafeVault. It has mature
filesystem, FUSE, operating-system, and GUI libraries. It may be preferred
when integrating with an existing C++ codebase or platform SDK.

Rust was chosen because this project combines:

- Low-level filesystem work.
- Persistent storage.
- Concurrent processing.
- Security-sensitive behavior.
- Long-running services.

Rust provides a strong balance of native performance and compile-time safety
for these requirements.

## 5.10 Balanced conclusion

Rust is not automatically faster or safer in every design. Good architecture,
testing, threat modeling, and operational controls remain necessary. Rust's
main advantage for SafeVault is that it makes many dangerous memory and
concurrency mistakes harder to express while retaining the performance needed
for filesystem software.
