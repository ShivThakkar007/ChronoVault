# ChronoVault project context

## Purpose and constraints

ChronoVault is a Windows 11 compatible, local, offline-first backup and file version control tool for a college OOP project. It uses C++17, CMake, the standard library, and `std::filesystem`, without external dependencies.

## Completed milestones

### Sprint 1: scanner and CLI foundation

`FileInfo` contains a root-relative path, byte size, and modification time. The const `FileScanner::scan` operation recursively lists regular files and reports per-path issues. Empty folders are valid; invalid paths are reported. `.chronovault` and `.git` directories are excluded. The scanner does not write to the scan target.

### Sprint 2: repository and full backups

`Repository` owns the repository root, initializes `.chronovault/snapshots` and `.chronovault/staging`, and reads/writes versioned text manifests. `BackupEngine` composes a repository with the scanner, snapshots one regular file or a directory, preserves directory structure and source modification times in the copy, and returns metadata only after publication. Snapshot IDs combine a high-resolution timestamp, process ID, and in-process atomic sequence.

Backup flow: validate source and repository paths; scan and reject partial scans; create a unique staging directory; copy and verify each regular file; write the manifest; atomically move the complete staging directory into `snapshots`. On Windows `MoveFileExW` uses `MOVEFILE_WRITE_THROUGH` without replace or cross-volume-copy flags. The whole directory becomes visible only after its manifest and all copied files are in place, and an existing destination is never replaced. If a move fails, the destination is inspected so an ID collision is distinguished from an actual access/permission error. Snapshot directories without a manifest are treated as uncommitted and omitted from history; other manifest inspection errors are surfaced. RAII cleanup removes staging data on handled failures and reports cleanup errors.

Snapshot IDs contain a high-resolution timestamp, process ID, and atomic in-process sequence. The process component prevents simultaneous ChronoVault processes from generating the same ID at the same clock tick; the atomic no-replace publish remains the final collision guard. Backup test fixture directories also include the process ID and use exclusive directory creation to avoid reusing stale or concurrent test data. The Sprint 2 manifest format is unchanged. Source and repository paths may not contain one another. Symbolic-link sources are rejected and linked files are not followed by the scanner.

#### Sprint 3 Windows publication regression investigation

The Windows error was reproduced intermittently on repeated `backup_tests` runs. The captured operation returned Win32 error 5 (`ERROR_ACCESS_DENIED`; formatted with `FormatMessageW`). At failure, the staged source still existed with ordinary directory attributes; the destination did not exist (its inspection returned error 2); both paths resolved to `C:\`; the destination parent existed; staged entries had no read-only, hidden, system, or reparse attributes; the manifest was readable; and the program's streams and directory iterators had closed. Immediate post-failure probes granted source delete and destination child-create access. This rules out an ID collision, a pre-created destination, a cross-volume move, persistent ACL denial, and handles left open by ChronoVault. The evidence points to a transient Windows filesystem/filter denial at the rename operation; the exact external actor is not identifiable from the filesystem API. Failure diagnostics now include source and destination paths, the immediate Win32 error code/message, and filesystem/access state captured after the final retry as well as before the first attempt.

`MoveFileExW` with `MOVEFILE_WRITE_THROUGH` and no replace/copy flags is a documented directory-move pattern. The previous implementation made one atomic attempt, so a transient error 5 aborted the backup. Publication now retries error 5 and sharing/lock violations with bounded backoff only when the destination remains absent, the source remains present, paths remain on the same volume, and directory access probes permit the operation. It never falls back to copy/delete. Existing IDs remain protected by the API's no-replace behavior. Failed attempts retain the captured Win32 code/message and state diagnostics, and RAII cleans staging. Fixture roots use process IDs and exclusive creation to prevent cross-process test interference. Persistent access denial remains an error and aborts the backup after bounded retries. The Sprint 2 manifest format and existing repositories are unchanged.

The `snapshot.cvmeta` manifest uses a version header and line-oriented fields. Paths are percent-encoded UTF-8 bytes, tracked file records include relative path, byte size, and native filesystem modification-time ticks. Snapshot listing reads persisted metadata and sorts newest first.

### Sprint 3: version history and change detection

`Version` encapsulates snapshot identity, per-source version number, creation time, source path, and tracked file count. `VersionManager` reconstructs version numbers from compatible Sprint 2 manifests by sorting each source's snapshots by creation time and snapshot ID. It exposes deterministic history and rejects comparisons from unrelated source paths or in reverse order. No existing snapshot metadata is rewritten.

`ChangeDetector` compares tracked relative paths and streams actual snapshot bytes using bounded 64 KiB buffers. It identifies added, modified, deleted, and unchanged files, including binary, empty, nested, whitespace, and Unicode paths. Size and modification timestamps are metadata only and do not determine content equality. Missing, symlinked, or unreadable snapshot content is returned as an issue and omitted from change counts rather than silently marked unchanged.

The CLI adds version history and two-snapshot comparison. The first snapshot for a source is version 1 and is shown as having no previous version. Different sources maintain independent sequences.

Incremental backups and restoration remain out of scope. Future work may add cleanup of stale staging directories with an explicit concurrency-safe policy.
