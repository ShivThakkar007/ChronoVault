#pragma once

#include "chronovault/ChangeDetector.hpp"
#include "chronovault/Repository.hpp"

#include <filesystem>
#include <optional>
#include <vector>

namespace chronovault {

class VersionManager {
public:
    explicit VersionManager(Repository repository);

    // Version numbers are derived independently for each canonical source path.
    [[nodiscard]] std::vector<Version> history(
        const std::optional<std::filesystem::path>& sourceDirectory = std::nullopt) const;
    [[nodiscard]] SnapshotComparison compare(const std::string& olderSnapshotId,
                                             const std::string& newerSnapshotId) const;

private:
    Repository repository_;
    ChangeDetector changeDetector_;
};

} // namespace chronovault
