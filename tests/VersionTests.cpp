#include "chronovault/BackupEngine.hpp"
#include "chronovault/VersionManager.hpp"

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
            path_ = fs::current_path() / ("chronovault-version-test-" + std::to_string(process)
                + "-" + std::to_string(stamp) + "-" + std::to_string(sequence.fetch_add(1)));
            std::error_code error;
            if (fs::create_directory(path_, error)) return;
            if (error) throw fs::filesystem_error("Cannot create version test fixture", path_, error);
        }
        throw std::runtime_error("Could not allocate a unique version test directory.");
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

void writeBytes(const fs::path& path, const std::string& bytes) {
    fs::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    if (!output) throw std::runtime_error("Could not write test fixture: " + path.string());
}

const chronovault::FileChange* findChange(const chronovault::SnapshotComparison& comparison,
                                          const fs::path& relativePath) {
    for (const auto& change : comparison.files) {
        if (change.relativePath == relativePath) return &change;
    }
    return nullptr;
}

void testContentComparisonAndDeterministicHistory() {
    TemporaryDirectory temp;
    const auto repositoryPath = temp.path() / "repository";
    const auto source = temp.path() / "source";
    const auto oldChangedTime = fs::file_time_type::clock::now() - std::chrono::hours(2);
    writeBytes(source / "changed.txt", "ABCD");
    fs::last_write_time(source / "changed.txt", oldChangedTime);
    writeBytes(source / "deleted.txt", "old");
    const char binaryContents[] = {'\0', 'A', static_cast<char>(0xFF), 'B'};
    writeBytes(source / "keep.bin", std::string(binaryContents, sizeof(binaryContents)));
    const auto originalKeepTime = fs::last_write_time(source / "keep.bin");
    writeBytes(source / "empty.dat", "");
    const auto unicodeRelative = fs::u8path(u8"space dir/雪 文件.bin");
    writeBytes(source / unicodeRelative, "same bytes");

    chronovault::BackupEngine backup{chronovault::Repository(repositoryPath)};
    const auto first = backup.createBackup(source);

    writeBytes(source / "changed.txt", "WXYZ"); // same length and restored timestamp
    fs::last_write_time(source / "changed.txt", oldChangedTime);
    fs::last_write_time(source / "keep.bin", originalKeepTime + std::chrono::hours(1));
    fs::remove(source / "deleted.txt");
    writeBytes(source / "added nested" / "new file.txt", "new");
    const auto second = backup.createBackup(source);

    chronovault::VersionManager manager{chronovault::Repository(repositoryPath)};
    const auto comparison = manager.compare(first.id, second.id);
    const auto counts = comparison.summary();
    require(counts.added == 1 && counts.modified == 1 && counts.deleted == 1
            && counts.unchanged == 3, "comparison should classify the complete inventory");
    require(comparison.issues.empty(), "valid snapshot files should be readable");
    const auto* modified = findChange(comparison, fs::path("changed.txt"));
    require(modified != nullptr && modified->type == chronovault::ChangeType::Modified,
            "same-size content change must be detected despite matching timestamps");
    const auto* deleted = findChange(comparison, fs::path("deleted.txt"));
    require(deleted != nullptr && deleted->type == chronovault::ChangeType::Deleted,
            "missing newer inventory entry should be classified as deleted");
    const auto* added = findChange(comparison, fs::path("added nested") / "new file.txt");
    require(added != nullptr && added->type == chronovault::ChangeType::Added,
            "new nested file should be classified as added");
    const auto* unicode = findChange(comparison, unicodeRelative);
    require(unicode != nullptr && unicode->type == chronovault::ChangeType::Unchanged,
            "Unicode path and identical file should be unchanged");
    const auto* binary = findChange(comparison, fs::path("keep.bin"));
    require(binary != nullptr && binary->type == chronovault::ChangeType::Unchanged,
            "binary content should compare correctly");
    const auto* empty = findChange(comparison, fs::path("empty.dat"));
    require(empty != nullptr && empty->type == chronovault::ChangeType::Unchanged,
            "empty files should compare correctly");

    const auto history = chronovault::VersionManager(chronovault::Repository(repositoryPath)).history(source);
    require(history.size() == 2, "same source should have two versions");
    require(history[0].number() == 1 && history[1].number() == 2,
            "versions should be assigned chronologically per source");
    require(history[0].snapshotId() == first.id && history[1].snapshotId() == second.id,
            "history should have deterministic order");

    const auto reopenedHistory = chronovault::VersionManager(chronovault::Repository(repositoryPath)).history();
    require(reopenedHistory.size() == 2 && reopenedHistory.front() == history.front(),
            "version history should persist across manager instances");

    const auto firstSnapshotFile = repositoryPath / ".chronovault" / "snapshots" / first.id
        / "files" / "keep.bin";
    fs::remove(firstSnapshotFile);
    const auto unreadable = manager.compare(first.id, second.id);
    require(!unreadable.issues.empty(), "missing snapshot content must be reported as an issue");
    require(findChange(unreadable, fs::path("keep.bin")) == nullptr,
            "unreadable content must not be silently classified as unchanged");
}

void testFirstVersionAndIndependentSources() {
    TemporaryDirectory temp;
    const auto repositoryPath = temp.path() / "repository";
    const auto sourceA = temp.path() / "alpha";
    const auto sourceB = temp.path() / "beta";
    writeBytes(sourceA / "a.txt", "a");
    writeBytes(sourceB / "b.txt", "b");
    chronovault::BackupEngine backup{chronovault::Repository(repositoryPath)};
    const auto onlyA = backup.createBackup(sourceA);
    const auto onlyB = backup.createBackup(sourceB);

    chronovault::VersionManager manager{chronovault::Repository(repositoryPath)};
    const auto historyA = manager.history(sourceA);
    const auto historyB = manager.history(sourceB);
    require(historyA.size() == 1 && historyA.front().number() == 1,
            "the first snapshot should be version 1 with no prior version");
    require(historyB.size() == 1 && historyB.front().number() == 1,
            "a separate source should start its own version sequence");
    bool sameVersionRejected = false;
    try {
        (void)manager.compare(onlyA.id, onlyA.id);
    } catch (const std::invalid_argument&) {
        sameVersionRejected = true;
    }
    require(sameVersionRejected, "a first version cannot be compared to itself as a previous version");

    bool unrelatedSourcesRejected = false;
    try {
        (void)manager.compare(onlyA.id, onlyB.id);
    } catch (const std::invalid_argument&) {
        unrelatedSourcesRejected = true;
    }
    require(unrelatedSourcesRejected, "snapshots from unrelated sources must not be compared");
}

void testVersionOperatorsAreMeaningfulAndDeterministic() {
    using Clock = std::chrono::system_clock;
    const auto time = Clock::time_point(std::chrono::seconds(10));
    const chronovault::Version first("id-a", 1, time, fs::path("source"), 2);
    const chronovault::Version sameIdentity("id-a", 9, time + std::chrono::seconds(1), fs::path("source"), 8);
    const chronovault::Version second("id-b", 2, time, fs::path("source"), 3);
    require(first == sameIdentity, "version equality should represent snapshot identity");
    require(first < second, "version ordering should follow per-source version number");
}

int main() {
    try {
        testContentComparisonAndDeterministicHistory();
        testFirstVersionAndIndependentSources();
        testVersionOperatorsAreMeaningfulAndDeterministic();
        std::cout << "All Version tests passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Version test failure: " << error.what() << '\n';
        return 1;
    }
}
