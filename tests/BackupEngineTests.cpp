#include "chronovault/BackupEngine.hpp"

#include <chrono>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>

#ifdef _WIN32
#include <process.h>
#else
#include <unistd.h>
#endif

namespace fs = std::filesystem;

class TemporaryDirectory {
public:
    TemporaryDirectory() {
        static std::atomic<unsigned long long> sequence{0};
#ifdef _WIN32
        const auto process = static_cast<unsigned long long>(::_getpid());
#else
        const auto process = static_cast<unsigned long long>(::getpid());
#endif
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        for (int attempt = 0; attempt < 100; ++attempt) {
            path_ = fs::current_path() / ("chronovault-backup-test-" + std::to_string(process)
                + "-" + std::to_string(stamp) + "-" + std::to_string(sequence.fetch_add(1)));
            std::error_code error;
            if (fs::create_directory(path_, error)) return;
            if (error) throw fs::filesystem_error("Cannot create test fixture", path_, error);
        }
        throw std::runtime_error("Could not allocate a unique backup test directory.");
    }
    ~TemporaryDirectory() {
        std::error_code ignored;
        fs::remove_all(path_, ignored);
    }
    const fs::path& path() const noexcept { return path_; }
private:
    fs::path path_;
};

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

std::string readAll(const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

void testDirectoryBackupAndMetadataPersistence() {
    TemporaryDirectory temp;
    const auto source = temp.path() / "source folder";
    const auto repositoryPath = temp.path() / "repository";
    fs::create_directories(source / "nested");
    const std::string rootContents = "unchanged source";
    std::ofstream(source / "root file.txt", std::ios::binary) << rootContents;
    std::ofstream(source / "nested" / "data.bin", std::ios::binary) << "nested payload";

    chronovault::BackupEngine engine{chronovault::Repository(repositoryPath)};
    const auto created = engine.createBackup(source);
    require(created.trackedFiles.size() == 2, "directory snapshot should track both files");
    require(readAll(source / "root file.txt") == rootContents, "backup must leave source content unchanged");
    const auto stored = repositoryPath / ".chronovault" / "snapshots" / created.id / "files";
    require(readAll(stored / "root file.txt") == rootContents, "root file should be copied");
    require(readAll(stored / "nested" / "data.bin") == "nested payload", "nested structure should be preserved");
    require(!fs::exists(repositoryPath / ".chronovault" / "staging" / created.id),
            "successful staging directory should be published");

    const chronovault::Repository reopened(repositoryPath);
    const auto snapshots = reopened.listSnapshots();
    require(snapshots.size() == 1, "metadata should survive reopening the repository");
    require(snapshots.front().id == created.id, "snapshot ID should persist");
    require(snapshots.front().sourcePath == fs::weakly_canonical(source), "source path should persist");
    require(snapshots.front().trackedFiles.size() == 2, "tracked file metadata should persist");
    require(snapshots.front().trackedFiles[0].sizeBytes > 0, "tracked sizes should persist");
}

void testSingleFileBackupAndUniqueIds() {
    TemporaryDirectory temp;
    const auto source = temp.path() / "one.txt";
    const auto repositoryPath = temp.path() / "repository";
    std::ofstream(source) << "file backup";
    chronovault::BackupEngine engine{chronovault::Repository(repositoryPath)};
    const auto first = engine.createBackup(source);
    const auto second = engine.createBackup(source);
    require(first.id != second.id, "each backup should receive a unique ID");
    require(first.trackedFiles.size() == 1, "single-file backup should track one file");
    require(readAll(repositoryPath / ".chronovault" / "snapshots" / first.id / "files" / "one.txt")
                == "file backup", "single file should be stored under its filename");
    require(chronovault::Repository(repositoryPath).listSnapshots().size() == 2,
            "existing snapshots must not be overwritten");
}

void testOverlappingPathsAreRejectedWithoutSnapshot() {
    TemporaryDirectory temp;
    const auto source = temp.path() / "source";
    fs::create_directories(source);
    std::ofstream(source / "file.txt") << "data";
    const auto repositoryPath = source / "repo";
    chronovault::BackupEngine engine{chronovault::Repository(repositoryPath)};
    bool rejected = false;
    try {
        (void)engine.createBackup(source);
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    require(rejected, "repository within source should be rejected");
    require(chronovault::Repository(repositoryPath).listSnapshots().empty(),
            "rejected backup must not be visible as a snapshot");
}

void testFailedPublicationDoesNotReplaceExistingSnapshot() {
    TemporaryDirectory temp;
    const auto source = temp.path() / "source" / "payload.txt";
    const auto repositoryPath = temp.path() / "repository";
    std::filesystem::create_directories(source.parent_path());
    std::ofstream(source, std::ios::binary) << "original snapshot";
    const chronovault::Repository repository(repositoryPath);
    const auto original = chronovault::BackupEngine(repository).createBackup(source.parent_path());
    const auto originalFile = repository.snapshotsPath() / original.id / "files" / "payload.txt";

    const auto staged = repository.stagingPath() / original.id;
    fs::create_directories(staged / "files");
    std::ofstream(staged / "files" / "payload.txt", std::ios::binary) << "must not replace";
    chronovault::SnapshotMetadata conflicting = original;
    conflicting.trackedFiles.front().sizeBytes = 16;
    repository.writeMetadata(staged, conflicting);

    bool failed = false;
    std::string failureMessage;
    try {
        repository.publishSnapshot(original.id);
    } catch (const std::exception& error) {
        failed = true;
        failureMessage = error.what();
    }
    require(failed, "publishing an existing snapshot ID must fail");
    require(failureMessage.find("already exists") != std::string::npos,
            "Windows destination collision should be reported as an ID conflict");
#ifdef _WIN32
    require(failureMessage.find("Windows error ") != std::string::npos,
            "failed Windows move should retain its native error code for collision diagnosis");
#endif
    require(readAll(originalFile) == "original snapshot", "failed publication must preserve existing snapshot data");
    const auto listed = repository.listSnapshots();
    require(listed.size() == 1 && listed.front().id == original.id,
            "failed publication must not create an additional completed snapshot");
    require(!fs::exists(staged), "failed publication must clean its staging directory");

    const std::string incompleteId = "123456789-123-1";
    const auto incompleteDestination = repository.snapshotsPath() / incompleteId;
    fs::create_directory(incompleteDestination);
    const auto incompleteStaging = repository.stagingPath() / incompleteId;
    fs::create_directories(incompleteStaging / "files");
    std::ofstream(incompleteStaging / "files" / "payload.txt", std::ios::binary) << "partial";
    auto incompleteMetadata = original;
    incompleteMetadata.id = incompleteId;
    repository.writeMetadata(incompleteStaging, incompleteMetadata);
    bool incompletePublishFailed = false;
    try {
        repository.publishSnapshot(incompleteId);
    } catch (const std::exception&) {
        incompletePublishFailed = true;
    }
    require(incompletePublishFailed, "publication must fail when a destination ID already exists");
    require(repository.listSnapshots().size() == 1,
            "an incomplete destination directory must not be listed as a completed snapshot");
    require(!fs::exists(incompleteStaging), "failed publication must clean incomplete staging data");
}

int main() {
    try {
        testDirectoryBackupAndMetadataPersistence();
        testSingleFileBackupAndUniqueIds();
        testOverlappingPathsAreRejectedWithoutSnapshot();
        testFailedPublicationDoesNotReplaceExistingSnapshot();
        std::cout << "All BackupEngine tests passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "BackupEngine test failure: " << error.what() << '\n';
        return 1;
    }
}
