#include "chronovault/FileScanner.hpp"

#include <system_error>

namespace chronovault {
namespace {

bool isInternalDirectory(const std::filesystem::path& path) {
    const auto name = path.filename().string();
    return name == ".chronovault" || name == ".git";
}

void recordIssue(ScanResult& result, const std::filesystem::path& path,
                 const std::error_code& error) {
    result.issues.push_back({path, error.message()});
}

} // namespace

ScanResult FileScanner::scan(const std::filesystem::path& root) const {
    ScanResult result;
    std::error_code error;

    if (root.empty()) {
        result.issues.push_back({root, "Scan path is empty."});
        return result;
    }

    const auto rootStatus = std::filesystem::status(root, error);
    if (error) {
        recordIssue(result, root, error);
        return result;
    }
    if (!std::filesystem::exists(rootStatus)) {
        result.issues.push_back({root, "Path does not exist."});
        return result;
    }
    if (!std::filesystem::is_directory(rootStatus)) {
        result.issues.push_back({root, "Path is not a directory."});
        return result;
    }

    std::filesystem::recursive_directory_iterator iterator(
        root, std::filesystem::directory_options::none, error);
    const std::filesystem::recursive_directory_iterator end;
    if (error) {
        recordIssue(result, root, error);
        return result;
    }

    while (iterator != end) {
        const auto entryPath = iterator->path();
        std::error_code entryError;
        const auto status = iterator->symlink_status(entryError);

        if (entryError) {
            recordIssue(result, entryPath, entryError);
            iterator.increment(error);
            if (error) {
                recordIssue(result, entryPath, error);
                error.clear();
            }
            continue;
        }

        if (std::filesystem::is_directory(status) && !std::filesystem::is_symlink(status)
            && isInternalDirectory(entryPath)) {
            iterator.disable_recursion_pending();
        } else if (std::filesystem::is_regular_file(status)) {
            const auto size = iterator->file_size(entryError);
            if (entryError) {
                recordIssue(result, entryPath, entryError);
            } else {
                const auto modified = iterator->last_write_time(entryError);
                if (entryError) {
                    recordIssue(result, entryPath, entryError);
                } else {
                    result.files.push_back({std::filesystem::relative(entryPath, root, entryError),
                                            size, modified});
                    if (entryError) {
                        result.files.pop_back();
                        recordIssue(result, entryPath, entryError);
                    }
                }
            }
        }

        iterator.increment(error);
        if (error) {
            recordIssue(result, entryPath, error);
            error.clear();
        }
    }

    return result;
}

} // namespace chronovault
