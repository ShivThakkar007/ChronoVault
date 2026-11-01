#pragma once

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <string>

namespace chronovault {

class Version {
public:
    Version(std::string snapshotId, std::size_t versionNumber,
            std::chrono::system_clock::time_point createdAt,
            std::filesystem::path sourceDirectory, std::size_t trackedFileCount);

    [[nodiscard]] const std::string& snapshotId() const noexcept;
    [[nodiscard]] std::size_t number() const noexcept;
    [[nodiscard]] std::chrono::system_clock::time_point createdAt() const noexcept;
    [[nodiscard]] const std::filesystem::path& sourceDirectory() const noexcept;
    [[nodiscard]] std::size_t trackedFileCount() const noexcept;

    friend bool operator==(const Version& left, const Version& right) noexcept;
    friend bool operator<(const Version& left, const Version& right);

private:
    std::string snapshotId_;
    std::size_t number_;
    std::chrono::system_clock::time_point createdAt_;
    std::filesystem::path sourceDirectory_;
    std::size_t trackedFileCount_;
};

} // namespace chronovault
