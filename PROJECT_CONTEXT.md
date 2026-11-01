# ChronoVault project context

## Purpose and constraints

ChronoVault is a Windows 11 compatible, local, offline-first backup and file version control tool for a college OOP project. It uses C++17, CMake, the standard library, and `std::filesystem`, without external dependencies.

## Completed milestones

### Sprint 1: scanner and CLI foundation

`FileInfo` contains a root-relative path, byte size, and modification time. The const `FileScanner::scan` operation recursively lists regular files and reports per-path issues. Empty folders are valid; invalid paths are reported. `.chronovault` and `.git` directories are excluded. The scanner does not write to the scan target.

### Sprint 2: repository and full backups

`Repository` owns the repository root, initializes `.chronovault/snapshots` and `.chronovault/staging`, and reads/writes versioned text manifests. `BackupEngine` composes a repository with the scanner, snapshots one regular file or a directory, preserves directory structure and source modification times in the copy, and returns metadata only after publication. Snapshot IDs combine a high-resolution timestamp and an in-process atomic sequence.

Backup flow: validate source and repository paths; scan and reject partial scans; create a unique staging directory; copy and verify each regular file; write the manifest; atomically reserve a new snapshot directory; move the content and then the manifest into it. The manifest is the commit marker, so listing ignores incomplete destinations left by abrupt termination. RAII cleanup removes staging and reserved destination data on handled failures. Existing destinations are never replaced. Source and repository paths may not contain one another. Symbolic-link sources are rejected and linked files are not followed by the scanner.

The `snapshot.cvmeta` manifest uses a version header and line-oriented fields. Paths are percent-encoded UTF-8 bytes, tracked file records include relative path, byte size, and native filesystem modification-time ticks. Snapshot listing reads persisted metadata and sorts newest first.

Incremental backups and restoration remain out of scope. Future work may add cleanup of stale staging directories with an explicit concurrency-safe policy.
