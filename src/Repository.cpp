#include "chronovault/Repository.hpp"

#include <algorithm>
#include <chrono>
#include <cwchar>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <system_error>
#include <thread>
#include <utility>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace chronovault {
namespace {

class StagedDirectoryCleanup {
public:
    explicit StagedDirectoryCleanup(std::filesystem::path path) : path_(std::move(path)) {}
    ~StagedDirectoryCleanup() {
        if (active_) {
            std::error_code ignored;
            std::filesystem::remove_all(path_, ignored);
            if (ignored) std::cerr << "Could not clean failed snapshot staging directory "
                                   << path_.string() << ": " << ignored.message() << '\n';
        }
    }
    void release() noexcept { active_ = false; }
private:
    std::filesystem::path path_;
    bool active_ = true;
};

#ifdef _WIN32
std::string windowsErrorMessage(DWORD code) {
    LPWSTR buffer = nullptr;
    const DWORD length = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, code, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        reinterpret_cast<LPWSTR>(&buffer), 0, nullptr);
    if (length == 0 || buffer == nullptr) return "Windows error message unavailable";
    const int utf8Length = WideCharToMultiByte(CP_UTF8, 0, buffer, static_cast<int>(length),
                                               nullptr, 0, nullptr, nullptr);
    std::string message;
    if (utf8Length > 0) {
        message.resize(static_cast<std::size_t>(utf8Length));
        WideCharToMultiByte(CP_UTF8, 0, buffer, static_cast<int>(length),
                            message.data(), utf8Length, nullptr, nullptr);
    }
    LocalFree(buffer);
    while (!message.empty() && (message.back() == '\r' || message.back() == '\n')) message.pop_back();
    return message.empty() ? "Windows error message unavailable" : message;
}

std::string windowsAttributes(const std::filesystem::path& path) {
    const DWORD attributes = GetFileAttributesW(path.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES) {
        const DWORD code = GetLastError();
        return "absent/inaccessible (" + std::to_string(code) + ": " + windowsErrorMessage(code) + ")";
    }
    std::ostringstream output;
    output << "present, flags=0x" << std::hex << attributes;
    if ((attributes & FILE_ATTRIBUTE_READONLY) != 0) output << ", readonly";
    if ((attributes & FILE_ATTRIBUTE_HIDDEN) != 0) output << ", hidden";
    if ((attributes & FILE_ATTRIBUTE_SYSTEM) != 0) output << ", system";
    if ((attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) output << ", reparse-point";
    if ((attributes & FILE_ATTRIBUTE_DIRECTORY) != 0) output << ", directory";
    return output.str();
}

std::string volumeRoot(const std::filesystem::path& path) {
    std::wstring buffer(32768, L'\0');
    if (!GetVolumePathNameW(path.c_str(), buffer.data(), static_cast<DWORD>(buffer.size()))) {
        const DWORD code = GetLastError();
        return "unavailable (" + std::to_string(code) + ": " + windowsErrorMessage(code) + ")";
    }
    buffer.resize(std::wcslen(buffer.c_str()));
    return std::filesystem::path(buffer).u8string();
}

struct AccessProbeResult {
    bool granted = true;
    DWORD error = ERROR_SUCCESS;
};

AccessProbeResult accessProbe(const std::filesystem::path& path, DWORD desiredAccess) {
    const HANDLE handle = CreateFileW(path.c_str(), desiredAccess,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        return {false, GetLastError()};
    }
    CloseHandle(handle);
    return {true, ERROR_SUCCESS};
}

std::string describeAccessProbe(const AccessProbeResult& result) {
    if (result.granted) return "granted";
    return "denied/locked (" + std::to_string(result.error) + ": "
        + windowsErrorMessage(result.error) + ")";
}

bool isTransientSharingError(DWORD code) {
    return code == ERROR_SHARING_VIOLATION || code == ERROR_LOCK_VIOLATION;
}

std::string stagedAttributeSummary(const std::filesystem::path& staged) {
    std::size_t entries = 0;
    std::size_t readOnly = 0;
    std::size_t hidden = 0;
    std::size_t system = 0;
    std::size_t reparse = 0;
    std::size_t attributeErrors = 0;
    DWORD lastAttributeError = ERROR_SUCCESS;
    std::error_code error;
    for (std::filesystem::recursive_directory_iterator iterator(staged, error), end;
         iterator != end && !error; iterator.increment(error)) {
        const DWORD attributes = GetFileAttributesW(iterator->path().c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES) {
            lastAttributeError = GetLastError();
            ++attributeErrors;
            continue;
        }
        ++entries;
        if ((attributes & FILE_ATTRIBUTE_READONLY) != 0) ++readOnly;
        if ((attributes & FILE_ATTRIBUTE_HIDDEN) != 0) ++hidden;
        if ((attributes & FILE_ATTRIBUTE_SYSTEM) != 0) ++system;
        if ((attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) ++reparse;
    }
    std::ostringstream output;
    output << "entries=" << entries << ", readonly=" << readOnly << ", hidden=" << hidden
           << ", system=" << system << ", reparse=" << reparse
           << ", attribute-errors=" << attributeErrors;
    if (attributeErrors != 0)
        output << ", last-attribute-error=" << lastAttributeError << ": "
               << windowsErrorMessage(lastAttributeError);
    if (error) output << ", enumeration-error=" << error.message();
    return output.str();
}

std::string publicationState(const std::filesystem::path& staged,
                             const std::filesystem::path& published,
                             const std::filesystem::path& snapshotsParent) {
    const auto sourceVolume = volumeRoot(staged);
    const auto destinationVolume = volumeRoot(snapshotsParent);
    std::ifstream manifest(staged / "snapshot.cvmeta", std::ios::binary);
    const bool manifestReadable = static_cast<bool>(manifest);
    std::ostringstream output;
    output << "source={" << windowsAttributes(staged) << "}, destination={"
           << windowsAttributes(published) << "}, destination-parent={"
           << windowsAttributes(snapshotsParent) << "}, source-volume={" << sourceVolume
           << "}, destination-volume={" << destinationVolume << "}, staged-entry-attributes={"
           << stagedAttributeSummary(staged)
           << "}, manifest=" << (manifestReadable ? "readable" : "unreadable-or-absent")
           << ", internal-streams-and-iterators=closed-before-move";
    return output.str();
}

bool sameVolume(const std::filesystem::path& staged,
                const std::filesystem::path& snapshotsParent) {
    const auto sourceVolume = volumeRoot(staged);
    const auto destinationVolume = volumeRoot(snapshotsParent);
    if (sourceVolume.compare(0, 13, "unavailable (") == 0
        || destinationVolume.compare(0, 13, "unavailable (") == 0) return false;
    return _wcsicmp(std::filesystem::path(sourceVolume).c_str(),
                    std::filesystem::path(destinationVolume).c_str()) == 0;
}
#endif

std::string encode(const std::string& value) {
    static constexpr char hex[] = "0123456789ABCDEF";
    std::string result;
    for (const unsigned char byte : value) {
        const bool safe = (byte >= 'a' && byte <= 'z') || (byte >= 'A' && byte <= 'Z')
            || (byte >= '0' && byte <= '9') || byte == '/' || byte == '.'
            || byte == '_' || byte == '-';
        if (safe) result.push_back(static_cast<char>(byte));
        else {
            result.push_back('%');
            result.push_back(hex[byte >> 4]);
            result.push_back(hex[byte & 0x0F]);
        }
    }
    return result;
}

int hexValue(char value) {
    if (value >= '0' && value <= '9') return value - '0';
    if (value >= 'A' && value <= 'F') return value - 'A' + 10;
    if (value >= 'a' && value <= 'f') return value - 'a' + 10;
    return -1;
}

std::string decode(const std::string& value) {
    std::string result;
    for (std::size_t index = 0; index < value.size(); ++index) {
        if (value[index] != '%') {
            result.push_back(value[index]);
            continue;
        }
        if (index + 2 >= value.size()) throw std::runtime_error("Invalid percent escape in manifest.");
        const int high = hexValue(value[index + 1]);
        const int low = hexValue(value[index + 2]);
        if (high < 0 || low < 0) throw std::runtime_error("Invalid percent escape in manifest.");
        result.push_back(static_cast<char>((high << 4) | low));
        index += 2;
    }
    return result;
}

std::string requireValue(const std::string& line, const std::string& key) {
    const auto prefix = key + "=";
    if (line.compare(0, prefix.size(), prefix) != 0)
        throw std::runtime_error("Malformed snapshot manifest: expected " + key + ".");
    return line.substr(prefix.size());
}

} // namespace

Repository::Repository(std::filesystem::path root)
    : root_(std::move(root)) {
    if (root_.empty()) throw std::invalid_argument("Repository path cannot be empty.");
    root_ = std::filesystem::absolute(root_).lexically_normal();
    dataPath_ = root_ / ".chronovault";
}

void Repository::initialize() const {
    std::error_code error;
    std::filesystem::create_directories(snapshotsPath(), error);
    if (error) throw std::filesystem::filesystem_error("Cannot initialize repository", root_, error);
    std::filesystem::create_directories(stagingPath(), error);
    if (error) throw std::filesystem::filesystem_error("Cannot initialize repository staging", root_, error);
}

const std::filesystem::path& Repository::root() const noexcept { return root_; }
std::filesystem::path Repository::snapshotsPath() const { return dataPath_ / "snapshots"; }
std::filesystem::path Repository::stagingPath() const { return dataPath_ / "staging"; }

void Repository::publishSnapshot(const std::string& id) const {
    if (id.empty() || id.find_first_not_of("0123456789-") != std::string::npos)
        throw std::invalid_argument("Snapshot ID contains invalid path characters.");

    const auto staged = stagingPath() / id;
    const auto published = snapshotsPath() / id;
    StagedDirectoryCleanup cleanup(staged);
    const auto metadata = readMetadata(staged / "snapshot.cvmeta");
    if (metadata.id != id)
        throw std::runtime_error("Staged snapshot ID does not match its directory name.");
    std::error_code error;
    if (!std::filesystem::is_directory(staged / "files", error) || error) {
        if (error) throw std::filesystem::filesystem_error("Cannot inspect staged snapshot files", staged / "files", error);
        throw std::runtime_error("Staged snapshot is missing its files directory.");
    }

#ifdef _WIN32
    // Same-volume rename is atomic. Omitting REPLACE_EXISTING and COPY_ALLOWED
    // prevents overwrites and non-atomic cross-volume copy fallbacks.
    const auto stateBeforeMove = publicationState(staged, published, snapshotsPath());
    const auto sourceVolume = volumeRoot(staged);
    const auto destinationVolume = volumeRoot(snapshotsPath());
    const bool pathsShareVolume = sameVolume(staged, snapshotsPath());
    const auto sourceAccess = accessProbe(staged, DELETE);
    const auto parentAccess = accessProbe(snapshotsPath(), FILE_ADD_SUBDIRECTORY);
    const bool sourceAccessAllowsAttempt = sourceAccess.granted
        || isTransientSharingError(sourceAccess.error);
    if (!pathsShareVolume || !sourceAccessAllowsAttempt || !parentAccess.granted)
        throw std::runtime_error("Snapshot publication preflight failed: same-volume="
            + std::string(pathsShareVolume ? "yes" : "no") + ", source-delete-access={"
            + describeAccessProbe(sourceAccess) + "}, destination-add-subdirectory-access={"
            + describeAccessProbe(parentAccess) + "}. Publication state: " + stateBeforeMove);

    constexpr int maxRetries = 6;
    DWORD code = ERROR_SUCCESS;
    DWORD destinationInspectError = ERROR_SUCCESS;
    AccessProbeResult lastSourceAccess{};
    AccessProbeResult lastParentAccess{};
    int retries = 0;
    for (int attempt = 0; attempt <= maxRetries; ++attempt) {
        if (MoveFileExW(staged.c_str(), published.c_str(), MOVEFILE_WRITE_THROUGH)) {
            cleanup.release();
            return;
        }
        // Capture immediately: every later Win32 call changes the thread's last-error value.
        code = GetLastError();
        const DWORD destinationAttributes = GetFileAttributesW(published.c_str());
        destinationInspectError = destinationAttributes == INVALID_FILE_ATTRIBUTES
            ? GetLastError() : ERROR_SUCCESS;
        if (destinationAttributes != INVALID_FILE_ATTRIBUTES)
            throw std::runtime_error("Snapshot ID already exists (Windows error "
                + std::to_string(code) + ": " + windowsErrorMessage(code)
                + "); refusing to overwrite it. Publication state: " + stateBeforeMove);

        if (attempt == maxRetries
            || (code != ERROR_ACCESS_DENIED && !isTransientSharingError(code))) break;

        lastSourceAccess = accessProbe(staged, DELETE);
        lastParentAccess = accessProbe(snapshotsPath(), FILE_ADD_SUBDIRECTORY);
        const DWORD currentSourceAttributes = GetFileAttributesW(staged.c_str());
        const DWORD sourceInspectError = currentSourceAttributes == INVALID_FILE_ATTRIBUTES
            ? GetLastError() : ERROR_SUCCESS;
        const bool sourceStillExists = currentSourceAttributes != INVALID_FILE_ATTRIBUTES
            && (currentSourceAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        const bool sourceAccessAllowsRetry = lastSourceAccess.granted
            || isTransientSharingError(lastSourceAccess.error);
        if (destinationInspectError != ERROR_FILE_NOT_FOUND
            && destinationInspectError != ERROR_PATH_NOT_FOUND) break;
        if (!sourceStillExists || sourceInspectError != ERROR_SUCCESS
            || !sourceAccessAllowsRetry || !lastParentAccess.granted || !pathsShareVolume) break;

        ++retries;
        std::this_thread::sleep_for(std::chrono::milliseconds(20 * (1 << attempt)));
    }

    // Refresh state after the final failed move. The initial snapshot above is
    // useful for context, but an external filter/process may have changed the
    // filesystem while the bounded retries were in progress.
    const auto finalPublicationState = publicationState(staged, published, snapshotsPath());
    throw std::filesystem::filesystem_error(
        "Cannot atomically publish completed snapshot (Windows error " + std::to_string(code)
            + ": " + windowsErrorMessage(code) + "; destination-inspection-error="
            + std::to_string(destinationInspectError) + " ("
            + windowsErrorMessage(destinationInspectError) + "); retries=" + std::to_string(retries)
            + "; source-access={" + describeAccessProbe(lastSourceAccess)
            + "}, destination-parent-access={" + describeAccessProbe(lastParentAccess)
            + "}; source-volume={" + sourceVolume + "}, destination-volume={"
            + destinationVolume + "}. Initial publication state: " + stateBeforeMove
            + ". Final publication state: " + finalPublicationState,
        staged, published,
        std::error_code(static_cast<int>(code), std::system_category()));
#else
    if (std::filesystem::exists(published, error))
        throw std::runtime_error("Snapshot ID already exists; refusing to overwrite it.");
    if (error) throw std::filesystem::filesystem_error(
        "Cannot inspect snapshot destination", published, error);
    std::filesystem::rename(staged, published, error);
    if (error) throw std::filesystem::filesystem_error(
        "Cannot publish completed snapshot", staged, published, error);
    cleanup.release();
#endif
}

std::vector<SnapshotMetadata> Repository::listSnapshots() const {
    std::vector<SnapshotMetadata> snapshots;
    std::error_code error;
    if (!std::filesystem::exists(snapshotsPath(), error)) {
        if (error) throw std::filesystem::filesystem_error("Cannot inspect repository", snapshotsPath(), error);
        return snapshots;
    }
    for (std::filesystem::directory_iterator iterator(snapshotsPath(), error), end;
         iterator != end; iterator.increment(error)) {
        if (error) throw std::filesystem::filesystem_error("Cannot list snapshots", snapshotsPath(), error);
        std::error_code typeError;
        if (!iterator->is_directory(typeError)) {
            if (typeError) throw std::filesystem::filesystem_error("Cannot inspect snapshot", iterator->path(), typeError);
            continue;
        }
        const auto manifest = iterator->path() / "snapshot.cvmeta";
        if (std::filesystem::is_regular_file(manifest, typeError)) {
            if (typeError) throw std::filesystem::filesystem_error("Cannot inspect snapshot manifest", manifest, typeError);
            auto metadata = readMetadata(manifest);
            if (metadata.id != iterator->path().filename().string())
                throw std::runtime_error("Snapshot directory and manifest ID do not match: " + iterator->path().string());
            snapshots.push_back(std::move(metadata));
        } else if (typeError) {
            if (typeError == std::errc::no_such_file_or_directory) continue;
            throw std::filesystem::filesystem_error("Cannot inspect snapshot manifest", manifest, typeError);
        }
    }
    std::sort(snapshots.begin(), snapshots.end(), [](const auto& left, const auto& right) {
        return left.createdAt > right.createdAt;
    });
    return snapshots;
}

void Repository::writeMetadata(const std::filesystem::path& directory,
                               const SnapshotMetadata& metadata) const {
    const auto manifest = directory / "snapshot.cvmeta";
    std::ofstream output(manifest, std::ios::binary | std::ios::out | std::ios::trunc);
    if (!output) throw std::runtime_error("Cannot create snapshot manifest: " + manifest.string());
    const auto created = std::chrono::duration_cast<std::chrono::milliseconds>(
        metadata.createdAt.time_since_epoch()).count();
    output << "CHRONOVAULT_SNAPSHOT 1\n"
           << "id=" << encode(metadata.id) << '\n'
           << "created_unix_ms=" << created << '\n'
           << "source=" << encode(metadata.sourcePath.u8string()) << '\n'
           << "file_count=" << metadata.trackedFiles.size() << '\n';
    for (const auto& file : metadata.trackedFiles) {
        output << "file=" << encode(file.relativePath.generic_u8string()) << '\t'
               << file.sizeBytes << '\t' << file.lastModified.time_since_epoch().count() << '\n';
    }
    output.flush();
    if (!output) throw std::runtime_error("Failed while writing snapshot manifest: " + manifest.string());
}

SnapshotMetadata Repository::readMetadata(const std::filesystem::path& file) const {
    std::ifstream input(file, std::ios::binary);
    if (!input) throw std::runtime_error("Cannot read snapshot manifest: " + file.string());
    std::string line;
    if (!std::getline(input, line) || line != "CHRONOVAULT_SNAPSHOT 1")
        throw std::runtime_error("Unsupported or malformed snapshot manifest: " + file.string());

    SnapshotMetadata metadata;
    if (!std::getline(input, line)) throw std::runtime_error("Manifest is missing snapshot ID.");
    metadata.id = decode(requireValue(line, "id"));
    if (!std::getline(input, line)) throw std::runtime_error("Manifest is missing creation time.");
    const auto created = std::stoll(requireValue(line, "created_unix_ms"));
    metadata.createdAt = std::chrono::system_clock::time_point(std::chrono::milliseconds(created));
    if (!std::getline(input, line)) throw std::runtime_error("Manifest is missing source path.");
    metadata.sourcePath = std::filesystem::u8path(decode(requireValue(line, "source")));
    if (!std::getline(input, line)) throw std::runtime_error("Manifest is missing file count.");
    const auto count = static_cast<std::size_t>(std::stoull(requireValue(line, "file_count")));
    metadata.trackedFiles.reserve(count);
    for (std::size_t index = 0; index < count; ++index) {
        if (!std::getline(input, line) || line.compare(0, 5, "file=") != 0)
            throw std::runtime_error("Manifest has a missing or malformed file record.");
        const auto first = line.find('\t', 5);
        const auto second = first == std::string::npos ? first : line.find('\t', first + 1);
        if (first == std::string::npos || second == std::string::npos)
            throw std::runtime_error("Manifest has a malformed file record.");
        FileInfo entry;
        entry.relativePath = std::filesystem::u8path(decode(line.substr(5, first - 5)));
        entry.sizeBytes = static_cast<std::uintmax_t>(std::stoull(line.substr(first + 1, second - first - 1)));
        using Duration = std::filesystem::file_time_type::duration;
        entry.lastModified = std::filesystem::file_time_type(Duration(
            static_cast<Duration::rep>(std::stoll(line.substr(second + 1)))));
        metadata.trackedFiles.push_back(std::move(entry));
    }
    if (std::getline(input, line)) throw std::runtime_error("Manifest contains unexpected trailing records.");
    if (input.bad()) throw std::runtime_error("Failed while reading snapshot manifest: " + file.string());
    return metadata;
}

} // namespace chronovault
