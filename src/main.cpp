#include "chronovault/FileScanner.hpp"
#include "chronovault/BackupEngine.hpp"

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

} // namespace

int main() {
    while (true) {
        std::cout << "\nChronoVault - Sprint 2\n"
                  << "1. Scan a directory\n"
                  << "2. Create a full backup\n"
                  << "3. List snapshots\n"
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
        else std::cout << "Please choose 1, 2, 3, or 0.\n";
    }
    return 0;
}
