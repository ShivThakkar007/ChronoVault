#include "chronovault/ChangeDetector.hpp"

#include <array>
#include <cstring>
#include <fstream>
#include <map>
#include <stdexcept>

namespace chronovault {
namespace {

using Inventory = std::map<std::string, const FileInfo*>;

Inventory makeInventory(const std::vector<FileInfo>& files) {
    Inventory result;
    for (const auto& file : files) {
        if (file.relativePath.empty() || file.relativePath.is_absolute()
            || file.relativePath.has_root_name() || file.relativePath.has_root_directory())
            throw std::runtime_error("Snapshot contains an unsafe relative file path.");
        for (const auto& component : file.relativePath) {
            if (component == "..") throw std::runtime_error("Snapshot file path escapes its snapshot.");
        }
        if (!result.emplace(file.relativePath.generic_u8string(), &file).second)
            throw std::runtime_error("Snapshot contains a duplicate relative file path: "
                                     + file.relativePath.generic_string());
    }
    return result;
}

std::filesystem::path contentPath(const std::filesystem::path& snapshotDirectory,
                                 const FileInfo& file) {
    return snapshotDirectory / "files" / file.relativePath;
}

void requireRegularSnapshotFile(const std::filesystem::path& path) {
    std::error_code error;
    const auto status = std::filesystem::symlink_status(path, error);
    if (error) throw std::filesystem::filesystem_error("Cannot inspect snapshot file", path, error);
    if (!std::filesystem::is_regular_file(status))
        throw std::runtime_error("Snapshot entry is not a regular file: " + path.string());
}

bool sameContent(const std::filesystem::path& leftPath,
                 const std::filesystem::path& rightPath) {
    requireRegularSnapshotFile(leftPath);
    requireRegularSnapshotFile(rightPath);
    std::ifstream left(leftPath, std::ios::binary);
    if (!left) throw std::runtime_error("Cannot read snapshot file: " + leftPath.string());
    std::ifstream right(rightPath, std::ios::binary);
    if (!right) throw std::runtime_error("Cannot read snapshot file: " + rightPath.string());

    std::array<char, 64 * 1024> leftBuffer{};
    std::array<char, 64 * 1024> rightBuffer{};
    while (true) {
        left.read(leftBuffer.data(), static_cast<std::streamsize>(leftBuffer.size()));
        right.read(rightBuffer.data(), static_cast<std::streamsize>(rightBuffer.size()));
        const auto leftCount = left.gcount();
        const auto rightCount = right.gcount();
        if (left.bad() || (left.fail() && !left.eof()))
            throw std::runtime_error("Read failed for snapshot file: " + leftPath.string());
        if (right.bad() || (right.fail() && !right.eof()))
            throw std::runtime_error("Read failed for snapshot file: " + rightPath.string());
        if (leftCount != rightCount
            || std::memcmp(leftBuffer.data(), rightBuffer.data(), static_cast<std::size_t>(leftCount)) != 0)
            return false;
        const bool leftEnded = left.eof();
        const bool rightEnded = right.eof();
        if (leftEnded || rightEnded) return leftEnded && rightEnded;
    }
}

void ensureReadable(const std::filesystem::path& path) {
    requireRegularSnapshotFile(path);
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("Cannot read snapshot file: " + path.string());
    std::array<char, 64 * 1024> buffer{};
    while (input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()))) {}
    if (input.bad() || (input.fail() && !input.eof()))
        throw std::runtime_error("Read failed for snapshot file: " + path.string());
}

void addChange(SnapshotComparison& result, const std::filesystem::path& relativePath,
               ChangeType type) {
    result.files.push_back({relativePath, type});
}

} // namespace

ChangeSummary SnapshotComparison::summary() const noexcept {
    ChangeSummary counts;
    for (const auto& file : files) {
        switch (file.type) {
        case ChangeType::Added: ++counts.added; break;
        case ChangeType::Modified: ++counts.modified; break;
        case ChangeType::Deleted: ++counts.deleted; break;
        case ChangeType::Unchanged: ++counts.unchanged; break;
        }
    }
    return counts;
}

SnapshotComparison ChangeDetector::compare(
    const Version& olderVersion, const SnapshotMetadata& olderSnapshot,
    const std::filesystem::path& olderSnapshotDirectory,
    const Version& newerVersion, const SnapshotMetadata& newerSnapshot,
    const std::filesystem::path& newerSnapshotDirectory) const {
    SnapshotComparison result{olderVersion, newerVersion, {}, {}};
    const auto olderFiles = makeInventory(olderSnapshot.trackedFiles);
    const auto newerFiles = makeInventory(newerSnapshot.trackedFiles);

    auto older = olderFiles.begin();
    auto newer = newerFiles.begin();
    while (older != olderFiles.end() || newer != newerFiles.end()) {
        if (newer == newerFiles.end()
            || (older != olderFiles.end() && older->first < newer->first)) {
            const auto& file = *older->second;
            try {
                ensureReadable(contentPath(olderSnapshotDirectory, file));
                addChange(result, file.relativePath, ChangeType::Deleted);
            } catch (const std::exception& error) {
                result.issues.push_back({file.relativePath, error.what()});
            }
            ++older;
            continue;
        }
        if (older == olderFiles.end() || newer->first < older->first) {
            const auto& file = *newer->second;
            try {
                ensureReadable(contentPath(newerSnapshotDirectory, file));
                addChange(result, file.relativePath, ChangeType::Added);
            } catch (const std::exception& error) {
                result.issues.push_back({file.relativePath, error.what()});
            }
            ++newer;
            continue;
        }

        const auto& oldFile = *older->second;
        const auto& newFile = *newer->second;
        try {
            const bool equal = sameContent(contentPath(olderSnapshotDirectory, oldFile),
                                           contentPath(newerSnapshotDirectory, newFile));
            addChange(result, oldFile.relativePath,
                      equal ? ChangeType::Unchanged : ChangeType::Modified);
        } catch (const std::exception& error) {
            result.issues.push_back({oldFile.relativePath, error.what()});
        }
        ++older;
        ++newer;
    }
    return result;
}

} // namespace chronovault
