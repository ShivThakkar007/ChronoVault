# ChronoVault project context

## Purpose

ChronoVault is a Windows 11 compatible, local, offline-first backup and file version control tool for a college OOP project. The implementation uses C++17, CMake, the standard library, and `std::filesystem`, with no external dependencies.

## Current milestone: Sprint 1

The application provides an interactive console menu and a read-only recursive scanner. `FileInfo` stores a root-relative path, size in bytes, and filesystem modification time. `FileScanner::scan` returns discovered files and per-path issues so one inaccessible entry does not hide successful results. Empty folders are a valid result; empty, missing, and non-directory paths are reported as issues. Permission-denied traversal is skipped safely. `.chronovault` and `.git` directories are excluded to prevent scanning vault or source-control metadata within the selected root.

The scanner follows directory entries but does not traverse symbolic-link directories; regular files discovered through normal traversal are reported. It performs no writes to the scan target. Backup, versioning, and restoration are out of scope until later sprints.

## Design

`FileInfo` is a small value type. `FileScanner` owns no persistent state and exposes one const operation. `ScanResult` and `ScanIssue` make partial success explicit without exceptions as the normal error path. The CLI is a thin presentation layer over the scanner. Tests use temporary directories with RAII cleanup.
