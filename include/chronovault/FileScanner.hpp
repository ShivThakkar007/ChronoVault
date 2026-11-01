#pragma once

#include "chronovault/FileInfo.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace chronovault {

struct ScanIssue {
    std::filesystem::path path;
    std::string message;
};

struct ScanResult {
    std::vector<FileInfo> files;
    std::vector<ScanIssue> issues;
};

class FileScanner {
public:
    // Scans regular files recursively. Vault and VCS metadata directories are skipped.
    [[nodiscard]] ScanResult scan(const std::filesystem::path& root) const;
};

} // namespace chronovault
