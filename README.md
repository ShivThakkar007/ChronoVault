# ChronoVault

ChronoVault is a local, offline-first backup and file version control tool. Sprint 2 adds full local snapshots for a file or directory. Scanning remains read-only; backup content is copied to a separate repository. Incremental backups and restoration are not implemented.

## Requirements

- CMake 3.16 or newer
- A C++17 compiler (Visual Studio 2019+, GCC, or Clang)

## Build and test

```powershell
cmake -S . -B build
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

## Use

Run `build\Release\chronovault.exe` with Visual Studio, or `build\chronovault.exe` with a single-configuration generator. The menu can scan a directory, create a full backup, list snapshots, or exit. Enter a repository directory and a source file or directory when prompted. The repository is initialized automatically on first backup.

Repository data is stored at `<repository>/.chronovault/`. Completed snapshots are under `snapshots/<snapshot-id>/`; copied content is under `files/` and the manifest is `snapshot.cvmeta`. Incomplete copies remain under `staging/` or may leave a destination directory without a manifest after abrupt termination; snapshot listing treats the manifest as the commit marker and ignores such directories. Existing destinations are reserved atomically and never replaced. Source and repository paths that contain one another are rejected.

### Manifest format

`snapshot.cvmeta` is UTF-8-compatible line-oriented text. The first line is `CHRONOVAULT_SNAPSHOT 1`; it is followed by `id`, `created_unix_ms`, `source`, and `file_count` fields. Each tracked file is represented by a `file=` record containing its percent-encoded relative path, size in bytes, and native filesystem modification-time tick count, separated by tabs. Percent encoding preserves delimiters, whitespace, and non-ASCII path bytes. Versioning the first line allows future format changes without a database dependency.

## Project layout

- `include/chronovault/`: public value types and class interfaces
- `src/`: scanner, repository, backup engine, and console application
- `tests/`: dependency-free scanner and backup tests
