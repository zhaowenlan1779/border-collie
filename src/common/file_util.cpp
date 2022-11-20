// Copyright 2022 Pengfei Zhu
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <filesystem>
#include <fstream>
#include <spdlog/spdlog.h>
#include "common/file_util.h"

namespace Common {

std::vector<u8> ReadFileContents(const std::u8string_view& path) {
    std::filesystem::path fs_path{path};
    std::ifstream file(fs_path, std::ios::ate | std::ios::binary);
    if (!file.is_open()) {
        SPDLOG_ERROR("Could not open file {}", fs_path.string());
        return {};
    }

    const std::size_t file_size = file.tellg();
    std::vector<u8> buffer(file_size);

    file.seekg(0);
    file.read(reinterpret_cast<char*>(buffer.data()), buffer.size());
    if (file.good()) {
        return buffer;
    } else {
        SPDLOG_ERROR("Failed to read from file {}", fs_path.string());
        return {};
    }
}

} // namespace Common
