// Copyright 2023 Pengfei Zhu
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <filesystem>
#include <fstream>
#include <optional>
#include "core/gltf/simdjson.h"

namespace GLTF {

class Container {
public:
    explicit Container(const std::filesystem::path& path);
    ~Container();

    simdjson::ondemand::parser parser;
    std::vector<char> json_data;
    simdjson::ondemand::document json;
    std::optional<std::ifstream> extra_buffer_file;
    std::size_t extra_buffer_offset{};
};

} // namespace GLTF
