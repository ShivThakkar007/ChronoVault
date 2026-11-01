#include "chronovault/BackupEngine.hpp"
#include "chronovault/RestoreEngine.hpp"

#include <atomic>
#include <chrono>
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
            path_ = fs::current_path() / ("chronovault-restore-test-" + std::to_string(process)
                + "-" + std::to_string(stamp) + "-" + std::to_string(sequence.fetch_add(1)));
            std::error_code error;
            if (fs::create_directory(path_, error)) return;
            if (error) throw fs::filesystem_error("Cannot create restore test fixture", path_, error);
        }
        throw std::runtime_error("Could not allocate a unique restore test directory.");
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

void writeBytes(const fs::path& path, const std::string& contents) {
    fs::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(contents.data(), static_cast<std::streamsize>(contents.size()));
    if (!output) throw std::runtime_error("Could not write test file: " + path.string());
}

std::string readBytes(const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("Could not read test file: " + path.string());
    return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

bool hasRestoreStaging(const fs::path& parent, const std::string& destinationName) {
    const auto prefix = destinationName + ".chronovault-restore-";
    for (const auto& entry : fs::directory_iterator(parent)) {
        if (entry.path().filename().u8string().compare(0, prefix.size(), prefix) == 0) return true;
    }
    return false;
}

chronovault::SnapshotMetadata makeMetadata(const std::string& id,
                                           const fs::path& source,
                                           const fs::path& relative,
                                           std::uintmax_t size,
                                           fs::file_time_type timestamp) {
    chronovault::SnapshotMetadata metadata;
    metadata.id = id;
    metadata.createdAt = std::chrono::system_clock::now();
    metadata.sourcePath = source;
    metadata.trackedFiles.push_back({relative, size, timestamp});
    return metadata;
}

void testDirectoryRestorePreservesContentsPathsAndTimestamps() {
    TemporaryDirectory temp;
    const auto source = temp.path() / "source";
    const auto repositoryPath = temp.path() / "repository";
    const char binaryBytes[] = {'\0', static_cast<char>(0x7F), static_cast<char>(0xFF),
                                'b', 'i', 'n', 'a', 'r', 'y'};
    const auto binary = std::string(binaryBytes, sizeof(binaryBytes));
    writeBytes(source / "nested" / "payload.bin", binary);
    writeBytes(source / "empty.txt", "");
    writeBytes(source / "root.txt", "root content");
    const auto requestedTimestamp = fs::file_time_type::clock::now() - std::chrono::hours(3);
    fs::last_write_time(source / "root.txt", requestedTimestamp);
    const auto expectedTimestamp = fs::last_write_time(source / "root.txt");

    chronovault::BackupEngine backup{chronovault::Repository(repositoryPath)};
    const auto snapshot = backup.createBackup(source);
    const auto originalSnapshotContent = readBytes(repositoryPath / ".chronovault" / "snapshots"
        / snapshot.id / "files" / "nested" / "payload.bin");

    const auto destination = temp.path() / "restored";
    const chronovault::RestoreEngine restore{chronovault::Repository(repositoryPath)};
    const auto preview = restore.preview(snapshot.id, destination);
    require(preview.filesToRestore == 3, "preview should report manifest file count");
    const auto result = restore.restore(snapshot.id, destination);
    require(result.filesRestored == 3 && result.filesFailed == 0,
            "successful restore should report all files and no failures");
    require(readBytes(destination / "nested" / "payload.bin") == binary,
            "binary contents and nested relative path should be preserved");
    require(readBytes(destination / "empty.txt").empty(), "empty files should be restored");
    require(readBytes(destination / "root.txt") == "root content", "root file should be restored");
    require(fs::last_write_time(destination / "root.txt") == expectedTimestamp,
            "file modification timestamp should be preserved when supported");
    require(readBytes(repositoryPath / ".chronovault" / "snapshots" / snapshot.id
                / "files" / "nested" / "payload.bin") == originalSnapshotContent,
            "restoration must not modify the original snapshot");
    require(!hasRestoreStaging(temp.path(), "restored"), "successful restore should publish and release staging");
}

void testSingleFileRestoreAndExistingDestinationRejection() {
    TemporaryDirectory temp;
    const auto source = temp.path() / "single.txt";
    const auto repositoryPath = temp.path() / "repository";
    writeBytes(source, "single file data");
    const auto snapshot = chronovault::BackupEngine{chronovault::Repository(repositoryPath)}.createBackup(source);
    const auto destination = temp.path() / "single-restore";
    const auto result = chronovault::RestoreEngine{chronovault::Repository(repositoryPath)}
        .restore(snapshot.id, destination);
    require(result.filesRestored == 1, "single-file snapshot should restore one file");
    require(readBytes(destination / "single.txt") == "single file data",
            "single-file snapshot should restore beneath the destination directory");

    const auto existing = temp.path() / "existing";
    fs::create_directory(existing);
    writeBytes(existing / "keep.txt", "do not overwrite");
    bool rejected = false;
    try {
        (void)chronovault::RestoreEngine{chronovault::Repository(repositoryPath)}.restore(snapshot.id, existing);
    } catch (const std::exception&) { rejected = true; }
    require(rejected, "existing destinations must be rejected");
    require(readBytes(existing / "keep.txt") == "do not overwrite",
            "existing user files must remain unchanged");
}

void testMissingAndIncompleteSnapshotsAreRejected() {
    TemporaryDirectory temp;
    const auto repositoryPath = temp.path() / "repository";
    const chronovault::Repository repository(repositoryPath);
    repository.initialize();
    chronovault::RestoreEngine restore(repository);
    bool missingRejected = false;
    try { (void)restore.restore("123456789-1-1", temp.path() / "missing"); }
    catch (const std::exception&) { missingRejected = true; }
    require(missingRejected, "unknown snapshot IDs must be rejected");

    const std::string incompleteId = "123456789-1-2";
    fs::create_directories(repository.snapshotsPath() / incompleteId / "files");
    bool incompleteRejected = false;
    try { (void)restore.restore(incompleteId, temp.path() / "incomplete"); }
    catch (const std::exception&) { incompleteRejected = true; }
    require(incompleteRejected, "snapshots without a manifest must be rejected as incomplete");
    require(!fs::exists(temp.path() / "incomplete"), "incomplete snapshot must not publish a destination");
}

void testTraversalAndRepositoryOverlapAreRejected() {
    TemporaryDirectory temp;
    const auto repositoryPath = temp.path() / "repository";
    const chronovault::Repository repository(repositoryPath);
    repository.initialize();
    const std::string id = "123456789-1-3";
    const auto snapshot = repository.snapshotsPath() / id;
    fs::create_directories(snapshot / "files");
    const auto outside = makeMetadata(id, temp.path() / "source", fs::path("../escape.txt"), 1,
                                      fs::file_time_type::clock::now());
    repository.writeMetadata(snapshot, outside);
    chronovault::RestoreEngine restore(repository);
    bool traversalRejected = false;
    try { (void)restore.preview(id, temp.path() / "traversal-destination"); }
    catch (const std::exception&) { traversalRejected = true; }
    require(traversalRejected, "manifest paths escaping the snapshot must be rejected");
    require(!fs::exists(temp.path() / "escape.txt"), "traversal must not create files outside the destination");

    bool overlapRejected = false;
    try { (void)restore.preview(id, repositoryPath / "inside"); }
    catch (const std::exception&) { overlapRejected = true; }
    require(overlapRejected, "restore destinations inside the repository must be rejected");
}

void testFailedRestoreCleansStagingAndNeverPublishesPartialFiles() {
    TemporaryDirectory temp;
    const auto repositoryPath = temp.path() / "repository";
    const chronovault::Repository repository(repositoryPath);
    repository.initialize();
    const std::string id = "123456789-1-4";
    const auto snapshot = repository.snapshotsPath() / id;
    const auto timestamp = fs::file_time_type::clock::now();
    fs::create_directories(snapshot / "files" / "nested");
    writeBytes(snapshot / "files" / "nested" / "first.txt", "copied then rollback");
    auto metadata = makeMetadata(id, temp.path() / "source", fs::path("nested/first.txt"),
                                 20, timestamp);
    metadata.trackedFiles.push_back({fs::path("later-missing.txt"), 7, timestamp});
    repository.writeMetadata(snapshot, metadata);

    const auto destination = temp.path() / "failed-restore";
    bool failed = false;
    try {
        (void)chronovault::RestoreEngine(repository).restore(id, destination);
    } catch (const std::exception&) { failed = true; }
    require(failed, "missing snapshot content must fail restoration");
    require(!fs::exists(destination), "a partial restore must never be published");
    require(!hasRestoreStaging(temp.path(), "failed-restore"), "failed restore staging data should be cleaned");
    require(readBytes(snapshot / "files" / "nested" / "first.txt") == "copied then rollback",
            "failed restore must leave snapshot data intact");
}

int main() {
    try {
        testDirectoryRestorePreservesContentsPathsAndTimestamps();
        testSingleFileRestoreAndExistingDestinationRejection();
        testMissingAndIncompleteSnapshotsAreRejected();
        testTraversalAndRepositoryOverlapAreRejected();
        testFailedRestoreCleansStagingAndNeverPublishesPartialFiles();
        std::cout << "All RestoreEngine tests passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "RestoreEngine test failure: " << error.what() << '\n';
        return 1;
    }
}
