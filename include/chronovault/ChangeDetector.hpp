#pragma once

#include "chronovault/Repository.hpp"
#include "chronovault/Version.hpp"

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

namespace chronovault {

enum class ChangeType { Added, Modified, Deleted, Unchanged };

struct FileChange {
    std::filesystem::path relativePath;
    ChangeType type;
};

struct ComparisonIssue {
    std::filesystem::path relativePath;
    std::string message;
};

struct ChangeSummary {
    std::size_t added = 0;
    std::size_t modified = 0;
    std::size_t deleted = 0;
    std::size_t unchanged = 0;
};

struct SnapshotComparison {
    Version olderVersion;
    Version newerVersion;
    std::vector<FileChange> files;
    std::vector<ComparisonIssue> issues;

    [[nodiscard]] ChangeSummary summary() const noexcept;
};

class ChangeDetector {
public:
    [[nodiscard]] SnapshotComparison compare(
        const Version& olderVersion, const SnapshotMetadata& olderSnapshot,
        const std::filesystem::path& olderSnapshotDirectory,
        const Version& newerVersion, const SnapshotMetadata& newerSnapshot,
        const std::filesystem::path& newerSnapshotDirectory) const;
};

} // namespace chronovault
