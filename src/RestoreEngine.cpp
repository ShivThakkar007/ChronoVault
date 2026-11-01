#include "chronovault/RestoreEngine.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <fstream>
#include <iostream>
#include <iterator>
#include <set>
#include <stdexcept>
#include <system_error>
#include <thread>
#include <utility>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <process.h>
#else
#include <unistd.h>
#endif

namespace chronovault {
namespace {

class StagingCleanup {
public:
    explicit StagingCleanup(std::filesystem::path path) : path_(std::move(path)) {}
    ~StagingCleanup() {
        if (!active_) return;
        std::error_code error;
        std::filesystem::remove_all(path_, error);
        if (error) std::cerr << "Could not clean failed restore staging directory "
                             << path_.string() << ": " << error.message() << '\n';
    }
    void release() noexcept { active_ = false; }
private:
    std::filesystem::path path_;
    bool active_ = true;
};

bool pathWithin(const std::filesystem::path& child, const std::filesystem::path& parent) {
    auto childPart = child.begin();
    auto parentPart = parent.begin();
    for (; parentPart != parent.end(); ++parentPart, ++childPart) {
        if (childPart == child.end()) return false;
#ifdef _WIN32
        if (_wcsicmp(childPart->c_str(), parentPart->c_str()) != 0) return false;
#else
        if (*childPart != *parentPart) return false;
#endif
    }
    return true;
}

std::filesystem::path normalizedAbsolute(const std::filesystem::path& path) {
    std::error_code error;
    const auto result = std::filesystem::absolute(path, error).lexically_normal();
    if (error) throw std::filesystem::filesystem_error("Cannot resolve restore destination", path, error);
    return result;
}

void validateRelativePath(const std::filesystem::path& path) {
    if (path.empty() || path.is_absolute() || path.has_root_name() || path.has_root_directory())
        throw std::runtime_error("Snapshot contains an invalid absolute or empty file path.");
    for (const auto& component : path) {
        const auto text = component.u8string();
        if (text.empty() || text == "." || text == ".." || text.find(':') != std::string::npos
            || text.find('\\') != std::string::npos)
            throw std::runtime_error("Snapshot contains an unsafe relative file path: " + path.string());
    }
}

void validateSnapshotFilePath(const std::filesystem::path& root,
                              const std::filesystem::path& relative) {
    auto current = root;
    std::size_t index = 0;
    const auto componentCount = static_cast<std::size_t>(std::distance(relative.begin(), relative.end()));
    for (const auto& component : relative) {
        current /= component;
        ++index;
        std::error_code error;
        const auto status = std::filesystem::symlink_status(current, error);
        if (error) throw std::filesystem::filesystem_error("Cannot inspect snapshot file", current, error);
        if (std::filesystem::is_symlink(status))
            throw std::runtime_error("Snapshot content contains a symbolic link: " + current.string());
        const bool finalComponent = index == componentCount;
        if ((finalComponent && !std::filesystem::is_regular_file(status))
            || (!finalComponent && !std::filesystem::is_directory(status)))
            throw std::runtime_error("Snapshot content is incomplete or has an invalid path: " + current.string());
    }
}

std::filesystem::path createStagingDirectory(const std::filesystem::path& destination) {
    static std::atomic<unsigned long long> sequence{0};
#ifdef _WIN32
    const auto process = static_cast<unsigned long long>(::_getpid());
#else
    const auto process = static_cast<unsigned long long>(::getpid());
#endif
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    for (int attempt = 0; attempt < 100; ++attempt) {
        auto candidate = destination.parent_path() /
            (destination.filename().u8string() + ".chronovault-restore-"
             + std::to_string(process) + "-" + std::to_string(stamp) + "-"
             + std::to_string(sequence.fetch_add(1)));
        std::error_code error;
        if (std::filesystem::create_directory(candidate, error)) return candidate;
        if (error) throw std::filesystem::filesystem_error("Cannot create restore staging directory", candidate, error);
    }
    throw std::runtime_error("Could not allocate a unique restore staging directory.");
}

void publishDirectory(const std::filesystem::path& staged,
                      const std::filesystem::path& destination) {
#ifdef _WIN32
    // Both paths are siblings. Do not request replacement or cross-volume copying.
    DWORD code = ERROR_SUCCESS;
    constexpr int maxRetries = 6;
    for (int attempt = 0; attempt <= maxRetries; ++attempt) {
        if (MoveFileExW(staged.c_str(), destination.c_str(), MOVEFILE_WRITE_THROUGH)) return;
        // Preserve the Win32 error before inspecting paths or formatting diagnostics.
        code = GetLastError();
        const DWORD destinationAttributes = GetFileAttributesW(destination.c_str());
        if (destinationAttributes != INVALID_FILE_ATTRIBUTES)
            throw std::runtime_error("Restore destination appeared before publication; refusing to overwrite it.");
        const DWORD destinationInspectError = GetLastError();
        if (attempt == maxRetries
            || (code != ERROR_ACCESS_DENIED && code != ERROR_SHARING_VIOLATION
                && code != ERROR_LOCK_VIOLATION)
            || (destinationInspectError != ERROR_FILE_NOT_FOUND
                && destinationInspectError != ERROR_PATH_NOT_FOUND)) break;
        std::error_code sourceError;
        if (!std::filesystem::is_directory(staged, sourceError) || sourceError) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(20 * (1 << attempt)));
    }
    const auto nativeError = std::error_code(static_cast<int>(code), std::system_category());
    throw std::filesystem::filesystem_error(
        "Cannot atomically publish restored directory (Windows error " + std::to_string(code)
            + ": " + nativeError.message() + ")",
        staged, destination, nativeError);
#else
    std::error_code error;
    if (std::filesystem::exists(destination, error))
        throw std::runtime_error("Restore destination already exists; refusing to overwrite it.");
    if (error) throw std::filesystem::filesystem_error("Cannot inspect restore destination", destination, error);
    std::filesystem::rename(staged, destination, error);
    if (error) throw std::filesystem::filesystem_error("Cannot publish restored directory", staged, destination, error);
#endif
}

} // namespace

RestoreEngine::RestoreEngine(Repository repository) : repository_(std::move(repository)) {}

RestorePreview RestoreEngine::preview(const std::string& snapshotId,
                                      const std::filesystem::path& destination) const {
    if (snapshotId.empty() || snapshotId.find_first_not_of("0123456789-") != std::string::npos)
        throw std::invalid_argument("Snapshot ID is invalid.");
    const auto snapshots = repository_.listSnapshots();
    const auto found = std::find_if(snapshots.begin(), snapshots.end(), [&](const auto& item) {
        return item.id == snapshotId;
    });
    if (found == snapshots.end()) throw std::runtime_error("Completed snapshot not found: " + snapshotId);

    const auto destinationPath = normalizedAbsolute(destination);
    if (destinationPath.filename().empty() || destinationPath == destinationPath.root_path())
        throw std::invalid_argument("Restore destination must name a new directory.");
    std::error_code error;
    if (std::filesystem::exists(destinationPath, error))
        throw std::runtime_error("Restore destination already exists; choose a new directory.");
    if (error) throw std::filesystem::filesystem_error("Cannot inspect restore destination", destinationPath, error);

    const auto parent = destinationPath.parent_path();
    if (!std::filesystem::is_directory(parent, error) || error) {
        if (error) throw std::filesystem::filesystem_error("Cannot inspect restore destination parent", parent, error);
        throw std::runtime_error("Restore destination parent directory does not exist.");
    }
    std::error_code canonicalError;
    const auto canonicalRepository = std::filesystem::weakly_canonical(repository_.root(), canonicalError);
    if (canonicalError) throw std::filesystem::filesystem_error("Cannot resolve repository path", repository_.root(), canonicalError);
    const auto canonicalDestinationParent = std::filesystem::weakly_canonical(parent, canonicalError);
    if (canonicalError) throw std::filesystem::filesystem_error("Cannot resolve destination parent", parent, canonicalError);
    const auto canonicalDestination = (canonicalDestinationParent / destinationPath.filename()).lexically_normal();
    if (pathWithin(canonicalDestination, canonicalRepository) || pathWithin(canonicalRepository, canonicalDestination))
        throw std::invalid_argument("Restore destination and repository paths must not overlap.");

    const auto snapshotDirectory = repository_.snapshotsPath() / snapshotId;
    const auto snapshotStatus = std::filesystem::symlink_status(snapshotDirectory, error);
    if (error) throw std::filesystem::filesystem_error("Cannot inspect snapshot directory", snapshotDirectory, error);
    if (std::filesystem::is_symlink(snapshotStatus) || !std::filesystem::is_directory(snapshotStatus))
        throw std::runtime_error("Snapshot directory is missing or is not a real directory.");
    const auto manifestPath = snapshotDirectory / "snapshot.cvmeta";
    const auto manifestStatus = std::filesystem::symlink_status(manifestPath, error);
    if (error) throw std::filesystem::filesystem_error("Cannot inspect snapshot manifest", manifestPath, error);
    if (std::filesystem::is_symlink(manifestStatus) || !std::filesystem::is_regular_file(manifestStatus))
        throw std::runtime_error("Snapshot is incomplete: its manifest is missing or invalid.");
    const auto metadata = repository_.readMetadata(snapshotDirectory / "snapshot.cvmeta");
    if (metadata.id != snapshotId) throw std::runtime_error("Snapshot manifest ID does not match its directory.");
    const auto filesPath = snapshotDirectory / "files";
    const auto filesStatus = std::filesystem::symlink_status(filesPath, error);
    if (error) throw std::filesystem::filesystem_error("Cannot inspect snapshot files", filesPath, error);
    if (std::filesystem::is_symlink(filesStatus) || !std::filesystem::is_directory(filesStatus)) {
        throw std::runtime_error("Snapshot is incomplete: its files directory is missing.");
    }
    std::set<std::string> uniquePaths;
    for (const auto& file : metadata.trackedFiles) {
        validateRelativePath(file.relativePath);
        if (!uniquePaths.insert(file.relativePath.generic_u8string()).second)
            throw std::runtime_error("Snapshot manifest contains duplicate file paths.");
    }
    return {snapshotId, destinationPath, metadata.trackedFiles.size()};
}

RestoreResult RestoreEngine::restore(const std::string& snapshotId,
                                     const std::filesystem::path& destination) const {
    const auto plan = preview(snapshotId, destination);
    const auto snapshotDirectory = repository_.snapshotsPath() / snapshotId;
    const auto metadata = repository_.readMetadata(snapshotDirectory / "snapshot.cvmeta");
    const auto staged = createStagingDirectory(plan.destination);
    StagingCleanup cleanup(staged);
    std::size_t restored = 0;

    for (const auto& file : metadata.trackedFiles) {
        const auto source = snapshotDirectory / "files" / file.relativePath;
        const auto target = staged / file.relativePath;
        validateSnapshotFilePath(snapshotDirectory / "files", file.relativePath);
        std::error_code error;
        std::filesystem::create_directories(target.parent_path(), error);
        if (error) throw std::filesystem::filesystem_error("Cannot create restored directory", target.parent_path(), error);
        std::filesystem::copy_file(source, target, std::filesystem::copy_options::none, error);
        if (error) throw std::filesystem::filesystem_error("Cannot restore snapshot file", source, target, error);
        const auto copiedSize = std::filesystem::file_size(target, error);
        if (error) throw std::filesystem::filesystem_error("Cannot verify restored file", target, error);
        if (copiedSize != file.sizeBytes) throw std::runtime_error("Restored file size does not match manifest: " + file.relativePath.string());
        std::filesystem::last_write_time(target, file.lastModified, error);
        if (error) throw std::filesystem::filesystem_error("Cannot preserve restored file timestamp", target, error);
        ++restored;
    }

    publishDirectory(staged, plan.destination);
    cleanup.release();
    return {snapshotId, plan.destination, restored, 0};
}

} // namespace chronovault
