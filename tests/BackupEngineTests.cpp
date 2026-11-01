#include "chronovault/BackupEngine.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>

namespace fs = std::filesystem;

class TemporaryDirectory {
public:
    TemporaryDirectory() {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        path_ = fs::temp_directory_path() / ("chronovault-backup-test-" + std::to_string(stamp));
        fs::create_directories(path_);
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

int main() {
    try {
        testDirectoryBackupAndMetadataPersistence();
        testSingleFileBackupAndUniqueIds();
        testOverlappingPathsAreRejectedWithoutSnapshot();
        std::cout << "All BackupEngine tests passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "BackupEngine test failure: " << error.what() << '\n';
        return 1;
    }
}
