#pragma once

#include "chronovault/Repository.hpp"

#include <cstddef>
#include <filesystem>
#include <string>

namespace chronovault {

struct RestorePreview {
    std::string snapshotId;
    std::filesystem::path destination;
    std::size_t filesToRestore = 0;
};

struct RestoreResult {
    std::string snapshotId;
    std::filesystem::path destination;
    std::size_t filesRestored = 0;
    std::size_t filesFailed = 0;
};

// Restores a completed snapshot into a new directory without changing the repository.
class RestoreEngine {
public:
    explicit RestoreEngine(Repository repository);

    [[nodiscard]] RestorePreview preview(const std::string& snapshotId,
                                         const std::filesystem::path& destination) const;
    [[nodiscard]] RestoreResult restore(const std::string& snapshotId,
                                        const std::filesystem::path& destination) const;

private:
    Repository repository_;
};

} // namespace chronovault
