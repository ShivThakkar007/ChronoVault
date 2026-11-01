#include "chronovault/Version.hpp"

#include <tuple>
#include <utility>

namespace chronovault {

Version::Version(std::string snapshotId, std::size_t versionNumber,
                 std::chrono::system_clock::time_point createdAt,
                 std::filesystem::path sourceDirectory, std::size_t trackedFileCount)
    : snapshotId_(std::move(snapshotId)), number_(versionNumber), createdAt_(createdAt),
      sourceDirectory_(std::move(sourceDirectory)), trackedFileCount_(trackedFileCount) {}

const std::string& Version::snapshotId() const noexcept { return snapshotId_; }
std::size_t Version::number() const noexcept { return number_; }
std::chrono::system_clock::time_point Version::createdAt() const noexcept { return createdAt_; }
const std::filesystem::path& Version::sourceDirectory() const noexcept { return sourceDirectory_; }
std::size_t Version::trackedFileCount() const noexcept { return trackedFileCount_; }

bool operator==(const Version& left, const Version& right) noexcept {
    return left.snapshotId_ == right.snapshotId_
        && left.sourceDirectory_ == right.sourceDirectory_;
}

bool operator<(const Version& left, const Version& right) {
    return std::tie(left.sourceDirectory_, left.number_, left.createdAt_, left.snapshotId_)
         < std::tie(right.sourceDirectory_, right.number_, right.createdAt_, right.snapshotId_);
}

} // namespace chronovault
