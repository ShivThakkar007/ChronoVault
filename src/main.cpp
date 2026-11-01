#include "chronovault/FileScanner.hpp"
#include "chronovault/BackupEngine.hpp"
#include "chronovault/RestoreEngine.hpp"
#include "chronovault/VersionManager.hpp"

#include <chrono>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>

namespace {

std::string formatTime(std::filesystem::file_time_type value) {
    const auto systemTime = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
        value - std::filesystem::file_time_type::clock::now() + std::chrono::system_clock::now());
    const std::time_t time = std::chrono::system_clock::to_time_t(systemTime);
    const std::tm* local = std::localtime(&time);
    if (local == nullptr) return "unknown";
    std::ostringstream output;
    output << std::put_time(local, "%Y-%m-%d %H:%M:%S");
    return output.str();
}

std::string formatTime(std::chrono::system_clock::time_point value) {
    const std::time_t time = std::chrono::system_clock::to_time_t(value);
    const std::tm* local = std::localtime(&time);
    if (local == nullptr) return "unknown";
    std::ostringstream output;
    output << std::put_time(local, "%Y-%m-%d %H:%M:%S");
    return output.str();
}

void scanDirectory() {
    std::cout << "Directory to scan: ";
    std::string input;
    std::getline(std::cin, input);
    if (input.empty()) {
        std::cout << "Please enter a directory path.\n";
        return;
    }

    const chronovault::FileScanner scanner;
    const auto result = scanner.scan(input);
    if (result.files.empty() && result.issues.empty()) {
        std::cout << "No files found (the folder may be empty).\n";
    } else {
        for (const auto& file : result.files) {
            std::cout << file.relativePath.generic_string() << "  |  "
                      << file.sizeBytes << " bytes  |  " << formatTime(file.lastModified) << '\n';
        }
        if (result.files.empty()) std::cout << "No files found.\n";
    }
    for (const auto& issue : result.issues) {
        std::cerr << "Could not scan " << issue.path.string() << ": " << issue.message << '\n';
    }
    std::cout << "Scanned " << result.files.size() << " file(s); "
              << result.issues.size() << " issue(s).\n";
}

std::string readPath(const std::string& prompt) {
    std::cout << prompt;
    std::string input;
    std::getline(std::cin, input);
    return input;
}

void createBackup() {
    const auto repositoryPath = readPath("Repository directory: ");
    const auto sourcePath = readPath("File or directory to back up: ");
    try {
        chronovault::BackupEngine engine{chronovault::Repository(repositoryPath)};
        const auto snapshot = engine.createBackup(sourcePath);
        std::cout << "Backup completed. Snapshot " << snapshot.id << " contains "
                  << snapshot.trackedFiles.size() << " file(s).\n";
    } catch (const std::exception& error) {
        std::cerr << "Backup failed: " << error.what() << '\n';
    }
}

void listSnapshots() {
    const auto repositoryPath = readPath("Repository directory: ");
    try {
        const chronovault::Repository repository(repositoryPath);
        const auto snapshots = repository.listSnapshots();
        if (snapshots.empty()) {
            std::cout << "No snapshots found.\n";
            return;
        }
        for (const auto& snapshot : snapshots) {
            std::cout << snapshot.id << "  |  " << formatTime(snapshot.createdAt)
                      << "  |  " << snapshot.trackedFiles.size() << " file(s)  |  "
                      << snapshot.sourcePath.string() << '\n';
        }
    } catch (const std::exception& error) {
        std::cerr << "Could not list snapshots: " << error.what() << '\n';
    }
}

const char* changeSymbol(chronovault::ChangeType type) {
    switch (type) {
    case chronovault::ChangeType::Added: return "+";
    case chronovault::ChangeType::Modified: return "M";
    case chronovault::ChangeType::Deleted: return "-";
    case chronovault::ChangeType::Unchanged: return "=";
    }
    return "?";
}

const char* changeLabel(chronovault::ChangeType type) {
    switch (type) {
    case chronovault::ChangeType::Added: return "Added";
    case chronovault::ChangeType::Modified: return "Modified";
    case chronovault::ChangeType::Deleted: return "Deleted";
    case chronovault::ChangeType::Unchanged: return "Unchanged";
    }
    return "Unknown";
}

void viewHistory() {
    const auto repositoryPath = readPath("Repository directory: ");
    try {
        const chronovault::VersionManager manager{chronovault::Repository(repositoryPath)};
        const auto versions = manager.history();
        if (versions.empty()) {
            std::cout << "No version history found.\n";
            return;
        }
        for (const auto& version : versions) {
            std::cout << "Version " << version.number() << "  |  " << version.snapshotId()
                      << "  |  " << formatTime(version.createdAt()) << "  |  "
                      << version.sourceDirectory().u8string() << "  |  "
                      << version.trackedFileCount() << " file(s)";
            if (version.number() == 1) std::cout << "  |  first version (no previous snapshot)";
            std::cout << '\n';
        }
    } catch (const std::exception& error) {
        std::cerr << "Could not read version history: " << error.what() << '\n';
    }
}

void compareSnapshots() {
    const auto repositoryPath = readPath("Repository directory: ");
    const auto olderId = readPath("Older snapshot ID: ");
    const auto newerId = readPath("Newer snapshot ID: ");
    try {
        const chronovault::VersionManager manager{chronovault::Repository(repositoryPath)};
        const auto comparison = manager.compare(olderId, newerId);
        std::cout << "\nSnapshot Comparison: version " << comparison.olderVersion.number()
                  << " -> version " << comparison.newerVersion.number() << "\n";
        for (const auto type : {chronovault::ChangeType::Added, chronovault::ChangeType::Modified,
                                chronovault::ChangeType::Deleted, chronovault::ChangeType::Unchanged}) {
            std::cout << '\n' << changeLabel(type) << " Files:\n";
            bool found = false;
            for (const auto& file : comparison.files) {
                if (file.type == type) {
                    std::cout << changeSymbol(type) << " " << file.relativePath.generic_string() << '\n';
                    found = true;
                }
            }
            if (!found) std::cout << "(none)\n";
        }
        const auto counts = comparison.summary();
        std::cout << "\nSummary:\nAdded: " << counts.added
                  << "\nModified: " << counts.modified
                  << "\nDeleted: " << counts.deleted
                  << "\nUnchanged: " << counts.unchanged << '\n';
        for (const auto& issue : comparison.issues) {
            std::cerr << "Could not compare " << issue.relativePath.generic_string()
                      << ": " << issue.message << '\n';
        }
    } catch (const std::exception& error) {
        std::cerr << "Could not compare snapshots: " << error.what() << '\n';
    }
}

void restoreSnapshot() {
    const auto repositoryPath = readPath("Repository directory: ");
    const auto snapshotId = readPath("Enter snapshot ID: ");
    const auto destination = readPath("Enter destination path: ");
    if (repositoryPath.empty() || snapshotId.empty() || destination.empty()) {
        std::cout << "Repository, snapshot ID, and destination are required.\n";
        return;
    }

    try {
        const chronovault::RestoreEngine engine{chronovault::Repository(repositoryPath)};
        const auto preview = engine.preview(snapshotId, destination);
        std::cout << "\nRestoration Preview:\n"
                  << "Snapshot ID: " << preview.snapshotId << '\n'
                  << "Files to restore: " << preview.filesToRestore << '\n'
                  << "Destination: " << preview.destination.string() << '\n'
                  << "Confirm restoration? (y/n): ";
        std::string confirmation;
        if (!std::getline(std::cin, confirmation)
            || (confirmation != "y" && confirmation != "Y")) {
            std::cout << "Restoration cancelled.\n";
            return;
        }

        const auto result = engine.restore(snapshotId, preview.destination);
        std::cout << "\nRestoration completed successfully.\n"
                  << "Files restored: " << result.filesRestored << '\n'
                  << "Files failed: " << result.filesFailed << '\n';
    } catch (const std::exception& error) {
        std::cerr << "Restoration failed; destination was not published. " << error.what()
                  << "\nFiles failed: 1\n";
    }
}

} // namespace

int main() {
    while (true) {
        std::cout << "\nChronoVault - Sprint 4\n"
                  << "1. Scan a directory\n"
                  << "2. Create a full backup\n"
                  << "3. List snapshots\n"
                  << "4. View version history\n"
                  << "5. Compare two snapshots\n"
                  << "6. Restore a snapshot\n"
                  << "0. Exit\n"
                  << "Choice: ";
        std::string choice;
        if (!std::getline(std::cin, choice)) break;
        if (choice == "0") {
            std::cout << "Goodbye.\n";
            return 0;
        }
        if (choice == "1") scanDirectory();
        else if (choice == "2") createBackup();
        else if (choice == "3") listSnapshots();
        else if (choice == "4") viewHistory();
        else if (choice == "5") compareSnapshots();
        else if (choice == "6") restoreSnapshot();
        else std::cout << "Please choose 1, 2, 3, 4, 5, 6, or 0.\n";
    }
    return 0;
}
