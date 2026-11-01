#include "chronovault/FileScanner.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace fs = std::filesystem;

class TemporaryDirectory {
public:
    TemporaryDirectory() {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        path_ = fs::current_path() / ("chronovault-test-" + std::to_string(stamp));
        fs::create_directories(path_);
    }
    ~TemporaryDirectory() {
        std::error_code ignored;
        fs::remove_all(path_, ignored);
    }
    const fs::path& path() const { return path_; }
private:
    fs::path path_;
};

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

void testListsFilesAndExcludesInternalData() {
    TemporaryDirectory temp;
    fs::create_directories(temp.path() / "nested");
    std::ofstream(temp.path() / "root.txt") << "abc";
    std::ofstream(temp.path() / "nested" / "child.txt") << "12345";
    fs::create_directories(temp.path() / ".chronovault" / "objects");
    std::ofstream(temp.path() / ".chronovault" / "objects" / "hidden.bin") << "hidden";
    fs::create_directories(temp.path() / ".git" / "objects");
    std::ofstream(temp.path() / ".git" / "objects" / "metadata") << "git";

    const auto result = chronovault::FileScanner{}.scan(temp.path());
    require(result.issues.empty(), "valid test tree should scan without issues");
    require(result.files.size() == 2, "should return only the two user files");
    bool rootFound = false;
    bool nestedFound = false;
    for (const auto& file : result.files) {
        if (file.relativePath == fs::path("root.txt")) {
            rootFound = true;
            require(file.sizeBytes == 3, "root file size should be reported");
        }
        if (file.relativePath == fs::path("nested") / "child.txt") {
            nestedFound = true;
            require(file.sizeBytes == 5, "nested file size should be reported");
        }
        require(file.lastModified != fs::file_time_type{}, "modification time should be populated");
    }
    require(rootFound && nestedFound, "paths should be relative to the scan root");
}

void testEmptyDirectory() {
    TemporaryDirectory temp;
    const auto result = chronovault::FileScanner{}.scan(temp.path());
    require(result.files.empty(), "empty directory should contain no files");
    require(result.issues.empty(), "empty directory should not be an error");
}

void testInvalidPaths() {
    TemporaryDirectory temp;
    const auto missing = chronovault::FileScanner{}.scan(temp.path() / "missing");
    require(!missing.issues.empty(), "missing path should report an issue");

    const auto file = temp.path() / "plain.txt";
    std::ofstream(file) << "x";
    const auto notDirectory = chronovault::FileScanner{}.scan(file);
    require(!notDirectory.issues.empty(), "file path should report that it is not a directory");

    const auto empty = chronovault::FileScanner{}.scan(fs::path{});
    require(!empty.issues.empty(), "empty path should report an issue");
}

int main() {
    try {
        testListsFilesAndExcludesInternalData();
        testEmptyDirectory();
        testInvalidPaths();
        std::cout << "All FileScanner tests passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FileScanner test failure: " << error.what() << '\n';
        return 1;
    }
}
