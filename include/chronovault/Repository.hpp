#pragma once

#include "chronovault/FileInfo.hpp"

#include <chrono>
#include <filesystem>
#include <string>
#include <vector>

namespace chronovault {

struct SnapshotMetadata {
    std::string id;
    std::chrono::system_clock::time_point createdAt;
    std::filesystem::path sourcePath;
    std::vector<FileInfo> trackedFiles;
};

// A repository owns the on-disk layout and manifest persistence for snapshots.
class Repository {
public:
    explicit Repository(std::filesystem::path root);

    void initialize() const;
    [[nodiscard]] const std::filesystem::path& root() const noexcept;
    [[nodiscard]] std::filesystem::path snapshotsPath() const;
    [[nodiscard]] std::filesystem::path stagingPath() const;
    [[nodiscard]] std::vector<SnapshotMetadata> listSnapshots() const;

    // Atomically moves a complete staged snapshot into place without replacing an existing ID.
    void publishSnapshot(const std::string& id) const;

    void writeMetadata(const std::filesystem::path& directory,
                       const SnapshotMetadata& metadata) const;
    [[nodiscard]] SnapshotMetadata readMetadata(const std::filesystem::path& file) const;

private:
    std::filesystem::path root_;
    std::filesystem::path dataPath_;
};

} // namespace chronovault
