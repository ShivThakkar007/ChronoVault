# TODO

## Sprint 1 — complete

- [x] Initialize the CMake project for C++17.
- [x] Add organized `include/`, `src/`, and `tests/` directories.
- [x] Implement `FileInfo`, recursive scanning, scan display, and invalid/empty path handling.
- [x] Exclude `.chronovault` and `.git` directories from source scans.
- [x] Add scanner tests and project documentation.

## Sprint 2 — complete

- [x] Implement repository initialization and storage path management.
- [x] Implement full backups of a single file and a directory.
- [x] Preserve relative directory structure and source modification times.
- [x] Generate unique snapshot identifiers and persist snapshot metadata.
- [x] Publish completed backups from staging and clean staging data on handled failures.
- [x] Reject overlapping repository/source paths and avoid replacing existing snapshots.
- [x] Add CLI backup and snapshot-list operations.
- [x] Add automated backup and metadata persistence tests.
- [x] Configure/build with CMake and run CTest (verified in Sprint 3's fresh Ninja build directory).

## Sprint 3 — implementation complete

- [x] Add encapsulated `Version` values with identity equality and deterministic ordering.
- [x] Derive chronological version numbers independently for each source without changing Sprint 2 manifests.
- [x] Compare file inventories and classify added, modified, deleted, and unchanged files from actual streamed content.
- [x] Handle binary, empty, nested, spaced, and Unicode paths with bounded memory use.
- [x] Report unreadable or missing snapshot content as issues and reject unrelated source comparisons.
- [x] Add CLI history and snapshot comparison options.
- [x] Add version ordering, history, persistence, and content comparison tests.
- [x] Configure/build in `build-sprint3-ninja` and run all CTest suites.

### Sprint 3 publication follow-up — complete

- [x] Publish the completed staging directory with Windows `MoveFileExW` write-through semantics, without replace or copy flags.
- [x] Inspect destination state after failed Windows moves to distinguish ID collisions from filesystem permission errors.
- [x] Add process IDs to generated snapshot IDs and exclusive process-scoped test fixtures to prevent shared-repository races.
- [x] Add collision coverage confirming failed publication leaves existing snapshot contents and history intact.
- [x] Rebuild and run all suites plus `version_tests` independently.
- [x] Document the reproduced Win32 error 5 state evidence, transient-denial inference, ID collision guard, and fixture isolation.
- [x] Capture Win32 error code/message and pre-publish source, destination, volume, attribute, manifest, and access state on failed publication.
- [x] Capture final filesystem state after publication retries and retry only transient Windows access/sharing errors under verified same-volume, no-collision, access-allowed conditions; retain atomic no-replace publication.

## Sprint 4 — file restoration complete

- [x] Add `RestoreEngine` with preview and restore operations composed over `Repository`.
- [x] Validate completed snapshots, manifest paths, repository boundaries, and a new destination directory.
- [x] Restore manifest-listed files through a sibling staging directory, verify copied sizes, and preserve timestamps where supported.
- [x] Publish the restored directory atomically without replacing existing destinations; clean staging on handled failures.
- [x] Add CLI preview, confirmation, and restore reporting.
- [x] Add isolated tests for directory and single-file restores, nested/empty/binary files, timestamps, missing/incomplete snapshots, traversal, collisions, cleanup, and snapshot immutability.
- [x] Build and run the new restoration suite independently and with all existing CTest suites.

## Later sprints

- [ ] Implement incremental backups.
- [ ] Implement restoration.
- [ ] Define safe handling for stale staging directories after abrupt termination.
