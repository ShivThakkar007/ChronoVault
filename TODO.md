# TODO

## Sprint 1 — complete

- [x] Initialize CMake project for C++17.
- [x] Create organized `include/`, `src/`, and `tests/` directories.
- [x] Add `FileInfo` with relative path, size, and last modification time.
- [x] Implement recursive, read-only `FileScanner` with per-entry error reporting.
- [x] Display relative paths, sizes, and modification times in the console.
- [x] Handle invalid paths, empty directories, and permission-denied traversal.
- [x] Exclude `.chronovault` and `.git` directories within the scan root.
- [x] Add basic scanner tests and project documentation.
- [ ] Configure/build with CMake and run CTest (pending availability of CMake in the environment).

## Later sprints

- [ ] Define backup storage format and repository configuration.
- [ ] Implement backup creation and file version history.
- [ ] Implement safe restore workflows.
