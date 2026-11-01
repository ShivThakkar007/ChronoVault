#include "chronovault/VersionManager.hpp"

#include <algorithm>
#include <cwchar>
#include <stdexcept>
#include <utility>

namespace chronovault {
namespace {

bool sameSource(const std::filesystem::path& left, const std::filesystem::path& right) {
    const auto normalizedLeft = left.lexically_normal();
    const auto normalizedRight = right.lexically_normal();
#ifdef _WIN32
    return _wcsicmp(normalizedLeft.c_str(), normalizedRight.c_str()) == 0;
#else
    return normalizedLeft == normalizedRight;
#endif
}

bool earlierSnapshot(const SnapshotMetadata& left, const SnapshotMetadata& right) {
    if (left.createdAt != right.createdAt) return left.createdAt < right.createdAt;
    return left.id < right.id;
}

} // namespace

VersionManager::VersionManager(Repository repository)
    : repository_(std::move(repository)) {}

std::vector<Version> VersionManager::history(
    const std::optional<std::filesystem::path>& sourceDirectory) const {
    const auto snapshots = repository_.listSnapshots();
    std::optional<std::filesystem::path> normalizedSource;
    if (sourceDirectory) {
        std::error_code error;
        normalizedSource = std::filesystem::weakly_canonical(*sourceDirectory, error);
        if (error) throw std::filesystem::filesystem_error(
            "Cannot resolve source path for history", *sourceDirectory, error);
    }
    std::vector<const SnapshotMetadata*> selected;
    selected.reserve(snapshots.size());
    for (const auto& snapshot : snapshots) {
        if (!normalizedSource || sameSource(snapshot.sourcePath, *normalizedSource))
            selected.push_back(&snapshot);
    }

    std::sort(selected.begin(), selected.end(), [](const auto* left, const auto* right) {
        if (!sameSource(left->sourcePath, right->sourcePath))
            return left->sourcePath.generic_u8string() < right->sourcePath.generic_u8string();
        return earlierSnapshot(*left, *right);
    });

    std::vector<Version> result;
    result.reserve(selected.size());
    std::filesystem::path currentSource;
    std::size_t number = 0;
    bool haveSource = false;
    for (const auto* snapshot : selected) {
        if (!haveSource || !sameSource(currentSource, snapshot->sourcePath)) {
            currentSource = snapshot->sourcePath;
            number = 0;
            haveSource = true;
        }
        ++number;
        result.emplace_back(snapshot->id, number, snapshot->createdAt,
                            snapshot->sourcePath, snapshot->trackedFiles.size());
    }
    std::sort(result.begin(), result.end());
    return result;
}

SnapshotComparison VersionManager::compare(const std::string& olderSnapshotId,
                                           const std::string& newerSnapshotId) const {
    if (olderSnapshotId == newerSnapshotId)
        throw std::invalid_argument("Choose two different snapshot IDs.");

    const auto snapshots = repository_.listSnapshots();
    const SnapshotMetadata* olderSnapshot = nullptr;
    const SnapshotMetadata* newerSnapshot = nullptr;
    for (const auto& snapshot : snapshots) {
        if (snapshot.id == olderSnapshotId) olderSnapshot = &snapshot;
        if (snapshot.id == newerSnapshotId) newerSnapshot = &snapshot;
    }
    if (olderSnapshot == nullptr || newerSnapshot == nullptr)
        throw std::invalid_argument("One or both snapshot IDs were not found in this repository.");
    if (!sameSource(olderSnapshot->sourcePath, newerSnapshot->sourcePath))
        throw std::invalid_argument("Snapshots belong to different source directories and cannot be compared.");

    const auto versions = history(olderSnapshot->sourcePath);
    std::size_t olderNumber = 0;
    std::size_t newerNumber = 0;
    for (const auto& version : versions) {
        if (version.snapshotId() == olderSnapshotId) olderNumber = version.number();
        if (version.snapshotId() == newerSnapshotId) newerNumber = version.number();
    }
    if (olderNumber == 0 || newerNumber == 0 || olderNumber >= newerNumber)
        throw std::invalid_argument("The selected snapshots are not in older-to-newer order.");

    const auto olderVersion = Version(olderSnapshot->id, olderNumber, olderSnapshot->createdAt,
                                     olderSnapshot->sourcePath, olderSnapshot->trackedFiles.size());
    const auto newerVersion = Version(newerSnapshot->id, newerNumber, newerSnapshot->createdAt,
                                     newerSnapshot->sourcePath, newerSnapshot->trackedFiles.size());
    const auto olderDirectory = repository_.snapshotsPath() / olderSnapshot->id;
    const auto newerDirectory = repository_.snapshotsPath() / newerSnapshot->id;
    return changeDetector_.compare(olderVersion, *olderSnapshot, olderDirectory,
                                   newerVersion, *newerSnapshot, newerDirectory);
}

} // namespace chronovault
