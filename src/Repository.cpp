#include "chronovault/Repository.hpp"

#include <algorithm>
#include <fstream>
#include <stdexcept>
#include <system_error>
#include <utility>

namespace chronovault {
namespace {

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
