#include "chronovault/BackupEngine.hpp"

#include <atomic>
#include <chrono>
#include <cwchar>
#include <system_error>
#include <utility>

namespace chronovault {
namespace {

class StagingCleanup {
public:
    explicit StagingCleanup(std::filesystem::path path) : path_(std::move(path)) {}
    ~StagingCleanup() {
        if (active_) {
            std::error_code ignored;
            std::filesystem::remove_all(path_, ignored);
        }
    }
    void release() noexcept { active_ = false; }
private:
    std::filesystem::path path_;
    bool active_ = true;
};

class PublishedCleanup {
public:
    explicit PublishedCleanup(std::filesystem::path path) : path_(std::move(path)) {}
    ~PublishedCleanup() {
        if (active_) {
            std::error_code ignored;
            std::filesystem::remove_all(path_, ignored);
        }
    }
    void release() noexcept { active_ = false; }
private:
    std::filesystem::path path_;
    bool active_ = true;
};

std::string createId() {
    static std::atomic<unsigned long long> sequence{0};
    const auto now = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    return std::to_string(now) + "-" + std::to_string(sequence.fetch_add(1));
}

bool isWithin(const std::filesystem::path& child, const std::filesystem::path& parent) {
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

std::filesystem::path canonicalForComparison(const std::filesystem::path& path) {
    std::error_code error;
    const auto canonical = std::filesystem::weakly_canonical(path, error);
    if (error) throw std::filesystem::filesystem_error("Cannot resolve path", path, error);
    return canonical;
}

} // namespace

BackupEngine::BackupEngine(Repository repository) : repository_(std::move(repository)) {}

SnapshotMetadata BackupEngine::createBackup(const std::filesystem::path& source) const {
    if (source.empty()) throw std::invalid_argument("Backup source path cannot be empty.");

    std::error_code error;
    const auto sourceStatus = std::filesystem::symlink_status(source, error);
    if (error) throw std::filesystem::filesystem_error("Cannot inspect backup source", source, error);
    if (std::filesystem::is_symlink(sourceStatus))
        throw std::invalid_argument("Symbolic-link sources are not supported for backup.");
    if (!std::filesystem::is_regular_file(sourceStatus) && !std::filesystem::is_directory(sourceStatus))
        throw std::invalid_argument("Backup source must be a regular file or directory.");

    const auto canonicalSource = canonicalForComparison(source);
    const auto canonicalRepo = canonicalForComparison(repository_.root());
    if (isWithin(canonicalSource, canonicalRepo) || isWithin(canonicalRepo, canonicalSource))
        throw std::invalid_argument("Source and repository paths overlap; choose separate paths.");

    repository_.initialize();

    SnapshotMetadata metadata;
    metadata.createdAt = std::chrono::system_clock::now();
    metadata.sourcePath = canonicalSource;
    if (std::filesystem::is_regular_file(sourceStatus)) {
        std::error_code metadataError;
        FileInfo file;
        file.relativePath = source.filename();
        file.sizeBytes = std::filesystem::file_size(source, metadataError);
        if (metadataError) throw std::filesystem::filesystem_error("Cannot read source file size", source, metadataError);
        file.lastModified = std::filesystem::last_write_time(source, metadataError);
        if (metadataError) throw std::filesystem::filesystem_error("Cannot read source modification time", source, metadataError);
        metadata.trackedFiles.push_back(std::move(file));
    } else {
        const auto scan = scanner_.scan(source);
        if (!scan.issues.empty())
            throw std::runtime_error("Backup aborted because scanning reported an issue at "
                                     + scan.issues.front().path.string() + ": "
                                     + scan.issues.front().message);
        metadata.trackedFiles = scan.files;
    }

    const auto sourceBase = std::filesystem::is_regular_file(sourceStatus) ? source.parent_path() : source;
    std::filesystem::path stagingDirectory;
    std::filesystem::path publishedDirectory;
    bool allocated = false;
    for (int attempt = 0; attempt < 10; ++attempt) {
        metadata.id = createId();
        stagingDirectory = repository_.stagingPath() / metadata.id;
        publishedDirectory = repository_.snapshotsPath() / metadata.id;
        if (std::filesystem::exists(stagingDirectory) || std::filesystem::exists(publishedDirectory)) continue;
        if (std::filesystem::create_directory(stagingDirectory, error)) {
            allocated = true;
            break;
        }
        if (error) throw std::filesystem::filesystem_error("Cannot create backup staging directory", stagingDirectory, error);
    }
    if (!allocated) throw std::runtime_error("Could not allocate a unique snapshot ID.");
    StagingCleanup cleanup(stagingDirectory);

    for (const auto& file : metadata.trackedFiles) {
        if (file.relativePath.empty() || file.relativePath.is_absolute())
            throw std::runtime_error("Scanner returned an unsafe relative file path.");
        for (const auto& component : file.relativePath) {
            if (component == "..") throw std::runtime_error("Scanner returned a path escaping the source.");
        }
        const auto from = sourceBase / file.relativePath;
        const auto to = stagingDirectory / "files" / file.relativePath;
        std::filesystem::create_directories(to.parent_path(), error);
        if (error) throw std::filesystem::filesystem_error("Cannot create backup directory", to.parent_path(), error);
        const auto fromStatus = std::filesystem::symlink_status(from, error);
        if (error) throw std::filesystem::filesystem_error("Cannot inspect source file", from, error);
        if (!std::filesystem::is_regular_file(fromStatus))
            throw std::runtime_error("Source changed during backup or contains a non-regular file: " + from.string());
        std::filesystem::copy_file(from, to, std::filesystem::copy_options::none, error);
        if (error) throw std::filesystem::filesystem_error("Cannot copy source file", from, to, error);
        std::filesystem::last_write_time(to, file.lastModified, error);
        if (error) throw std::filesystem::filesystem_error("Cannot preserve source modification time", to, error);
        const auto copiedSize = std::filesystem::file_size(to, error);
        if (error) throw std::filesystem::filesystem_error("Cannot verify copied file", to, error);
        if (copiedSize != file.sizeBytes)
            throw std::runtime_error("Source changed during backup; size verification failed: " + from.string());
    }

    repository_.writeMetadata(stagingDirectory, metadata);
    // Reserve the destination atomically so a concurrent backup can never replace it.
    if (!std::filesystem::create_directory(publishedDirectory, error)) {
        if (error) throw std::filesystem::filesystem_error(
            "Cannot reserve snapshot destination", publishedDirectory, error);
        throw std::runtime_error("Snapshot ID already exists; refusing to overwrite it.");
    }
    PublishedCleanup publishedCleanup(publishedDirectory);
    std::filesystem::rename(stagingDirectory / "files", publishedDirectory / "files", error);
    if (error) throw std::filesystem::filesystem_error(
        "Cannot publish snapshot files", stagingDirectory / "files", publishedDirectory / "files", error);
    // The manifest is the commit marker: listing ignores a destination until this final rename succeeds.
    std::filesystem::rename(stagingDirectory / "snapshot.cvmeta",
                            publishedDirectory / "snapshot.cvmeta", error);
    if (error) throw std::filesystem::filesystem_error(
        "Cannot publish snapshot metadata", stagingDirectory / "snapshot.cvmeta",
        publishedDirectory / "snapshot.cvmeta", error);
    publishedCleanup.release();
    return metadata;
}

} // namespace chronovault
