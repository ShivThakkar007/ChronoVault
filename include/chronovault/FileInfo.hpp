#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>

namespace chronovault {

struct FileInfo {
    std::filesystem::path relativePath;
    std::uintmax_t sizeBytes = 0;
    std::filesystem::file_time_type lastModified{};
};

} // namespace chronovault
