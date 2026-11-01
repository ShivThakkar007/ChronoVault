#pragma once

#include "chronovault/FileScanner.hpp"
#include "chronovault/Repository.hpp"

#include <filesystem>

namespace chronovault {

class BackupEngine {
public:
    explicit BackupEngine(Repository repository);

    // Creates one full snapshot from a regular file or a directory.
    [[nodiscard]] SnapshotMetadata createBackup(const std::filesystem::path& source) const;

private:
    Repository repository_;
    FileScanner scanner_;
};

} // namespace chronovault
