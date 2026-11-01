# ChronoVault

ChronoVault is a local, offline-first backup and file version control tool. It creates full local snapshots for a file or directory, assigns source-specific version numbers, compares snapshot contents, and restores a snapshot into a new directory. Scanning and comparison are read-only; backups and restores copy data without changing their source snapshots. Incremental backups are not implemented.

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

Run `build\Release\chronovault.exe` with Visual Studio, or `build\chronovault.exe` with a single-configuration generator. The menu can scan a directory, create a full backup, list snapshots, view version history, compare two snapshots, restore a snapshot, or exit. Enter a repository directory and a source file or directory when prompted. The repository is initialized automatically on first backup.

Repository data is stored at `<repository>/.chronovault/`. Completed snapshots are under `snapshots/<snapshot-id>/`; copied content is under `files/` and the manifest is `snapshot.cvmeta`. Backups are assembled completely under `staging/`, then the whole snapshot directory is moved into `snapshots/` on the same volume. Windows publication uses `MoveFileExW` with write-through, without replacement or copy-across-volume flags. Existing IDs are never overwritten. Transient access-denied or sharing failures are retried only while the source remains present, the destination absent, the paths remain on one volume, and the required directory access checks pass; permanent errors are reported with Win32 diagnostics. Staging directories and snapshot directories without a manifest are not listed as completed snapshots. Handled failures clean staging data; abrupt process termination may leave staging data. Source and repository paths that contain one another are rejected.

### Manifest format

`snapshot.cvmeta` is UTF-8-compatible line-oriented text. The first line is `CHRONOVAULT_SNAPSHOT 1`; it is followed by `id`, `created_unix_ms`, `source`, and `file_count` fields. Each tracked file is represented by a `file=` record containing its percent-encoded relative path, size in bytes, and native filesystem modification-time tick count, separated by tabs. Percent encoding preserves delimiters, whitespace, and non-ASCII path bytes. Versioning the first line allows future format changes without a database dependency.

## Version history and comparison

Version numbers are derived from the existing snapshot manifests, separately for each source path. Snapshots are ordered by creation time, with ID as a deterministic tie-breaker. This preserves compatibility with Sprint 2 metadata and persists history without adding manifest fields. File changes are determined by streaming the saved bytes in fixed-size buffers; stored sizes and timestamps are not used as proof that content is unchanged. Unreadable or missing snapshot files are reported as comparison issues and excluded from the change counts. Snapshots from different source paths cannot be compared.

## Restoring a snapshot

Choose **Restore a snapshot**, enter the repository, snapshot ID, and a destination directory that does not exist. ChronoVault previews the snapshot ID, file count, and destination before asking for confirmation. It validates the completed snapshot and manifest paths, copies tracked files into a unique sibling staging directory, verifies file sizes, restores modification timestamps where supported, then publishes the complete directory with a same-volume atomic rename. Existing destinations are never overwritten; restore destinations inside or overlapping the repository are rejected. A failure leaves no published partial destination and the staging directory is cleaned when safe. Restores reproduce files recorded by the manifest; empty directories are not tracked by the current snapshot format. Symbolic links are rejected.

## Project layout

- `include/chronovault/`: public value types and class interfaces
- `src/`: scanner, repository, backup engine, and console application
- `tests/`: dependency-free scanner, backup, version, and restoration tests
