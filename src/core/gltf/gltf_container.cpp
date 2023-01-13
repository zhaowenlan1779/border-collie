// Copyright 2023 Pengfei Zhu
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <spdlog/spdlog.h>
#include "common/alignment.h"
#include "common/swap.h"
#include "core/gltf/gltf_container.h"

namespace GLTF {

struct GLBHeader {
    u32_le magic;
    u32_le version;
    u32_le length;
};
static_assert(sizeof(GLBHeader) == 12);

struct GLBChunkHeader {
    u32_le length;
    u32_le type;
};
static_assert(sizeof(GLBChunkHeader) == 8);

constexpr u32 MakeMagic(char a, char b, char c, char d) {
    return a | b << 8 | c << 16 | d << 24;
}

static constexpr u32 GLBMagic = MakeMagic('g', 'l', 'T', 'F');
static constexpr u32 GLBVersion = 2;
static constexpr u32 JSONChunkMagic = MakeMagic('J', 'S', 'O', 'N');
static constexpr u32 BINChunkMagic = MakeMagic('B', 'I', 'N', 0);

Container::Container(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        SPDLOG_ERROR("Failed to open file {}", path.string());
        throw std::runtime_error("Failed to open file");
    }

    // Note: Valid glTF has to have an `asset' property and hence will be larger than the header
    GLBHeader header;
    file.read(reinterpret_cast<char*>(&header), sizeof(header));
    if (!file) {
        SPDLOG_ERROR("Failed to read header {}", path.string());
        throw std::runtime_error("Failed to read header");
    }

    if (header.magic == GLBMagic) {
        if (header.version != GLBVersion) {
            SPDLOG_ERROR("GLB is of unsupported version {}", header.version);
            throw std::runtime_error("GLB unsupported version");
        }

        GLBChunkHeader json_header;
        file.read(reinterpret_cast<char*>(&json_header), sizeof(json_header));
        if (!file || json_header.type != JSONChunkMagic) {
            SPDLOG_ERROR("First chunk of GLB is not JSON {}", path.string());
            throw std::runtime_error("First chunk of GLB should be JSON");
        }

        const auto json_chunk_size = Common::AlignUp(json_header.length, 4);
        json_data.resize(json_chunk_size + simdjson::SIMDJSON_PADDING);
        file.read(json_data.data(), json_chunk_size);

        json = parser.iterate(json_data.data(), json_chunk_size, json_data.size());

        GLBChunkHeader bin_header;
        file.read(reinterpret_cast<char*>(&bin_header), sizeof(bin_header));
        if (file && bin_header.type == BINChunkMagic) {
            extra_buffer_offset = file.tellg();
            extra_buffer_file = std::move(file);
        } else {
            SPDLOG_WARN("No valid BIN chunk {}", path.string());
        }
    } else { // Read as JSON
        file.seekg(0, std::ios::end);
        const auto json_size = file.tellg();
        file.seekg(0);

        json_data.resize(static_cast<std::size_t>(json_size) + simdjson::SIMDJSON_PADDING);
        file.read(json_data.data(), json_size);
        if (!file) {
            SPDLOG_ERROR("Failed to read data {}", path.string());
            throw std::runtime_error("Failed to read data");
        }

        json = parser.iterate(json_data.data(), json_size, json_data.size());
    }
}

Container::~Container() = default;

} // namespace GLTF
