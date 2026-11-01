cmake_minimum_required(VERSION 3.16)

project(ChronoVault VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(chronovault_core
    src/FileScanner.cpp
    src/Repository.cpp
    src/BackupEngine.cpp
    src/Version.cpp
    src/ChangeDetector.cpp
    src/VersionManager.cpp
)
target_include_directories(chronovault_core PUBLIC include)

if(MSVC)
    target_compile_options(chronovault_core PRIVATE /W4 /permissive-)
else()
    target_compile_options(chronovault_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(chronovault src/main.cpp)
target_link_libraries(chronovault PRIVATE chronovault_core)

include(CTest)
if(BUILD_TESTING)
    add_executable(scanner_tests tests/FileScannerTests.cpp)
    target_link_libraries(scanner_tests PRIVATE chronovault_core)
    add_test(NAME scanner_tests COMMAND scanner_tests)

    add_executable(backup_tests tests/BackupEngineTests.cpp)
    target_link_libraries(backup_tests PRIVATE chronovault_core)
    add_test(NAME backup_tests COMMAND backup_tests)

    add_executable(version_tests tests/VersionTests.cpp)
    target_link_libraries(version_tests PRIVATE chronovault_core)
    add_test(NAME version_tests COMMAND version_tests)
endif()
