# ChronoVault

ChronoVault is a local, offline-first backup and file version control tool. Sprint 1 provides the read-only directory scanner and an interactive console shell. Backup, version history, and restore are not implemented yet.

## Requirements

- CMake 3.16 or newer
- A C++17 compiler (Visual Studio 2019+, GCC, or Clang)

## Build

```powershell
cmake -S . -B build
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

Run `build\Release\chronovault.exe` with Visual Studio, or `build\chronovault.exe` with a single-configuration generator. Choose **Scan a directory** and enter the directory path. The scanner lists relative paths, byte sizes, and modification times. It skips `.chronovault` and `.git` directories and never modifies scanned files.

## Project layout

- `include/chronovault/`: public types and scanner interface
- `src/`: scanner implementation and console application
- `tests/`: dependency-free scanner tests
