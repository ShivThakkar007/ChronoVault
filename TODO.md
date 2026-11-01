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
- [ ] Configure/build with CMake and run CTest (CMake is installed, but this environment has no working C++ toolchain: NMake is absent and MinGW fails to start with a Windows DLL relocation error; LLVM installation stalled during download).

## Later sprints

- [ ] Implement incremental backups.
- [ ] Implement restoration.
- [ ] Define safe handling for stale staging directories after abrupt termination.
