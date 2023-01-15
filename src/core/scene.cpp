// Copyright 2023 Pengfei Zhu
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <map>
#include <ranges>
#include <type_traits>
#include <unordered_map>
#include <boost/pfr.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/hash.hpp>
#include <libbase64.h>
#include <mikktspace/mikktspace.h>
#include <spdlog/spdlog.h>
#include "common/assert.h"
#include "common/ranges.h"
#include "common/scope_exit.h"
#include "common/swap.h"
#include "core/gltf/gltf_container.h"
#include "core/gltf/json_helpers.hpp"
#include "core/scene.h"
#include "core/vulkan/vulkan_buffer.h"
#include "core/vulkan/vulkan_device.h"
#include "core/vulkan/vulkan_texture.h"

namespace Renderer {

BufferFile::BufferFile(const std::string_view& uri) {
    Load(uri);
}
BufferFile::BufferFile(SceneLoader& loader, const GLTF::Buffer& buffer) {
    if (buffer.uri.has_value()) {
        Load(*buffer.uri);
    } else if (loader.container.extra_buffer_file.has_value()) {
        // There should only be one such buffer
        file = std::move(loader.container.extra_buffer_file);
        offset = loader.container.extra_buffer_offset;
    } else {
        SPDLOG_ERROR("No URI but no GLB buffer either");
        throw std::runtime_error("No URI but no GLB buffer either");
    }
}
BufferFile::~BufferFile() = default;

static constexpr int HexCharToInt(char c) {
    if ('0' <= c && c <= '9') {
        return c - '0';
    } else if ('a' <= c && c <= 'f') {
        return c - 'a' + 10;
    } else if ('A' <= c && c <= 'F') {
        return c - 'A' + 10;
    }
    SPDLOG_ERROR("Invalid hex char {}", c);
    throw std::runtime_error("Invalid hex char");
}

void BufferFile::Load(const std::string_view& uri) {
    if (uri.starts_with("data:")) {
        // Skip past the MIME type.
        // For safety we cannot directly search for commas because the MIME type may contain them
        bool quoted = false;
        bool escape = false;
        std::size_t i = 5;
        for (; i < uri.size(); ++i) {
            if (!escape && uri[i] == '"') {
                quoted = !quoted;
            }
            if (!quoted && uri[i] == ',') { // Found the delimiter
                ++i;
                break;
            }
            escape = !escape && uri[i] == '\\';
        }
        if (i >= uri.size()) {
            SPDLOG_ERROR("Could not find data delimiter in data URI {}", uri);
            throw std::runtime_error("Could not find data delimiter");
        }

        const auto src_len = uri.size() - i;
        if (src_len % 4 != 0) {
            SPDLOG_ERROR("Base64 input size is incorrect {}", uri);
            throw std::runtime_error("Base64 input size is incorrect");
        }
        data.resize(src_len / 4 * 3);

        base64_decode(uri.data() + i, src_len, reinterpret_cast<char*>(data.data()), &file_size, 0);
        data.resize(file_size);
    } else {
        // Un-percent-encode the uri
        std::vector<char> decoded_str(uri.size() + 1);

        std::size_t i = 0, j = 0;
        while (i < uri.size()) {
            if (uri[i] == '%') {
                if (i >= uri.size() - 2) {
                    SPDLOG_ERROR("URI is improperly percent-encoded {}", uri);
                    throw std::runtime_error("URI is improperly percent-encoded");
                }
                decoded_str[j] =
                    static_cast<char>(HexCharToInt(uri[i + 1]) * 16 + HexCharToInt(uri[i + 2]));
                i += 3;
            } else {
                decoded_str[j] = uri[i];
                ++i;
            }
            ++j;
        }

        // Open file
        file = std::ifstream(std::filesystem::u8path(decoded_str.data()),
                             std::ios::ate | std::ios::binary);
        if (!*file) {
            SPDLOG_ERROR("Failed to open file {}", decoded_str.data());
            throw std::runtime_error("Failed to open file");
        }
        file_size = file->tellg();
        file->seekg(0);
    }
}

void BufferFile::Read(void* out, std::size_t size) {
    if (file) {
        file->read(reinterpret_cast<char*>(out), size);
        if (!*file) {
            SPDLOG_ERROR("Failed to read from file");
            throw std::runtime_error("Failed to read from file");
        }
    } else {
        std::memcpy(out, data.data() + pos, size);
        pos += size;
    }
}

void BufferFile::Seek(std::size_t new_pos) {
    if (file) {
        file->seekg(offset + new_pos);
        if (!*file) {
            SPDLOG_ERROR("Failed to seek file");
            throw std::runtime_error("Failed to seek file");
        }
    } else {
        pos = offset + new_pos;
    }
}

IndexBufferAccessor::IndexBufferAccessor() = default;

IndexBufferAccessor::IndexBufferAccessor(SceneLoader& loader, const GLTF::Accessor& accessor)
    : name(accessor.name.value_or("Unnamed")), component_type(accessor.component_type),
      type(accessor.type), count(accessor.count) {

    if (!accessor.buffer_view.has_value()) {
        // Note: Strictly speaking we should allow the case where no buffer_view is present,
        // but that's pretty much pointless for index buffers.
        SPDLOG_ERROR("Index buffer has no buffer view");
        throw std::runtime_error("Index buffer has no buffer view");
    }

    const auto total_size = GetTotalSize(accessor);

    const auto& buffer_view = loader.gltf.buffer_views[*accessor.buffer_view];
    auto& buffer_file = *loader.buffer_files.Get(loader, buffer_view.buffer);
    buffer_file.Seek(buffer_view.byte_offset + accessor.byte_offset);

    if (component_type == GLTF::Accessor::ComponentType::UnsignedByte) {
        component_type = GLTF::Accessor::ComponentType::UnsignedShort;
        // Convert into u16 as we upload it
        gpu_buffer = std::make_shared<VulkanImmUploadBuffer>(
            loader.device,
            VulkanBufferCreateInfo{total_size * 2, loader.index_buffer_params.usage,
                                   loader.index_buffer_params.dst_stage_mask,
                                   loader.index_buffer_params.dst_access_mask},
            [&buffer_file](void* data, std::size_t size) {
                std::vector<u8> temp_data(size);
                buffer_file.Read(temp_data.data(), size);
                for (std::size_t i = 0; i < size; ++i) {
                    *(reinterpret_cast<u16_le*>(data) + i) = temp_data[i];
                }
            });
    } else {
        GetIndexType(component_type); // Make sure we have an index type
        gpu_buffer = std::make_shared<VulkanImmUploadBuffer>(
            loader.device,
            VulkanBufferCreateInfo{total_size, loader.index_buffer_params.usage,
                                   loader.index_buffer_params.dst_stage_mask,
                                   loader.index_buffer_params.dst_access_mask},
            [&buffer_file](void* data, std::size_t size) { buffer_file.Read(data, size); });
    }
}

IndexBufferAccessor::~IndexBufferAccessor() = default;

VertexBufferView::VertexBufferView([[maybe_unused]] const SceneLoader& loader,
                                   const GLTF::BufferView& buffer_view_)
    : buffer_view(buffer_view_) {}

VertexBufferView::~VertexBufferView() = default;

void VertexBufferView::AddAccessor(const GLTF::Accessor& accessor) {
    if (buffer_view.byte_stride.has_value()) {
        const auto start = accessor.byte_offset / (*buffer_view.byte_stride);
        chunks.insert(boost::icl::interval<std::size_t>::right_open(start, start + accessor.count));
    } else {
        non_strided_accessor = &accessor;
    }
}

void VertexBufferView::Load(SceneLoader& loader) {
    if (!buffers.empty()) {
        return;
    }

    auto& buffer_file = *loader.buffer_files.Get(loader, buffer_view.buffer);
    if (buffer_view.byte_stride.has_value()) {
        const auto byte_stride = *buffer_view.byte_stride;
        for (const auto& chunk : chunks) {
            buffer_file.Seek(buffer_view.byte_offset + chunk.lower() * byte_stride);
            buffers.emplace(chunk.lower(),
                            std::make_shared<VulkanImmUploadBuffer>(
                                loader.device,
                                VulkanBufferCreateInfo{
                                    .size = (chunk.upper() - chunk.lower()) * byte_stride,
                                    .usage = loader.vertex_buffer_params.usage,
                                    .dst_stage_mask = loader.vertex_buffer_params.dst_stage_mask,
                                    .dst_access_mask = loader.vertex_buffer_params.dst_access_mask,
                                },
                                [&buffer_file](void* data, std::size_t size) {
                                    buffer_file.Read(data, size);
                                }));
        }
    } else {
        ASSERT(non_strided_accessor);

        buffer_file.Seek(buffer_view.byte_offset + non_strided_accessor->byte_offset);
        non_strided_buffer = std::make_shared<VulkanImmUploadBuffer>(
            loader.device,
            VulkanBufferCreateInfo{
                .size = GetTotalSize(*non_strided_accessor),
                .usage = loader.vertex_buffer_params.usage,
                .dst_stage_mask = loader.vertex_buffer_params.dst_stage_mask,
                .dst_access_mask = loader.vertex_buffer_params.dst_access_mask,
            },
            [&buffer_file](void* data, std::size_t size) { buffer_file.Read(data, size); });
    }
}

VertexBufferView::BufferInfo VertexBufferView::GetAccessorBufferInfo(
    const GLTF::Accessor& accessor) const {
    if (buffer_view.byte_stride.has_value()) {
        const auto byte_stride = *buffer_view.byte_stride;
        const auto start = accessor.byte_offset / byte_stride;
        const auto attribute_offset = accessor.byte_offset % byte_stride;

        const auto iter = chunks.find(start);
        ASSERT_MSG(iter != chunks.end(), "Failed to find interval containing accessor");
        return {
            .buffer = buffers.at(iter->lower()),
            .buffer_offset = (start - iter->lower()) * byte_stride,
            .attribute_offset = attribute_offset,
        };
    } else {
        return {
            .buffer = non_strided_buffer,
            .buffer_offset = 0,
            .attribute_offset = 0,
        };
    }
}

Sampler::Sampler(const SceneLoader& loader, const GLTF::Sampler& sampler)
    : name(sampler.name.value_or("Unnamed")), uses_mipmaps(IsMipmapUsed(sampler.min_filter)),
      sampler{*loader.device,
              vk::SamplerCreateInfo{
                  .magFilter = ToVkFilter(sampler.mag_filter),
                  .minFilter = ToVkFilter(sampler.min_filter),
                  .mipmapMode = GetMipmapMode(sampler.min_filter),
                  .addressModeU = ToAddressMode(sampler.wrapS),
                  .addressModeV = ToAddressMode(sampler.wrapT),
                  .mipLodBias = 0.0f,
                  .anisotropyEnable = VK_TRUE,
                  .maxAnisotropy =
                      loader.device.physical_device.getProperties().limits.maxSamplerAnisotropy,
                  .minLod = 0.0f,
                  .maxLod = 0.0f,
                  .borderColor = vk::BorderColor::eIntOpaqueBlack,
              }} {}

Sampler::~Sampler() = default;

Image::Image(SceneLoader& loader, const GLTF::Image& image) : name(image.name.value_or("Unnamed")) {
    if (image.buffer_view.has_value()) {
        const auto& buffer_view = loader.gltf.buffer_views[*image.buffer_view];
        auto& buffer_file = *loader.buffer_files.Get(loader, buffer_view.buffer);
        buffer_file.Seek(buffer_view.byte_offset);

        std::vector<u8> data(buffer_view.byte_length);
        buffer_file.Read(data.data(), data.size());
        texture = std::make_unique<VulkanTexture>(loader.device, std::move(data));
    } else if (image.uri.has_value()) {
        BufferFile buffer_file{*image.uri};
        std::vector<u8> data(buffer_file.file_size);
        buffer_file.Read(data.data(), data.size());
        texture = std::make_unique<VulkanTexture>(loader.device, std::move(data));
    } else {
        SPDLOG_ERROR("Image has no source");
        throw std::runtime_error("Image has no source");
    }
}
Image::~Image() = default;

Texture::Texture(SceneLoader& loader, const GLTF::Texture& texture)
    : name(texture.name.value_or("Unnamed")), image(loader.images.Get(loader, texture.source)) {
    if (texture.sampler.has_value()) {
        sampler = loader.samplers.Get(loader, *texture.sampler);
    }
}
Texture::~Texture() = default;

Material::Material(SceneLoader& loader, const GLTF::Material& material)
    : name(material.name.value_or("Unnamed")) {

    const auto LoadTexture = [&loader](const auto& json_field, int& index, u32& texcoord) {
        if (json_field.has_value()) {
            index = static_cast<int>(loader.textures.GetIndex(loader, json_field->index));
            texcoord = static_cast<u32>(json_field->texcoord);
            if (texcoord > 1) {
                SPDLOG_ERROR("Currently only 2 UVs are supported, but requested UV{}", texcoord);
                throw std::runtime_error("Unsupported number of UV");
            }
        } else {
            index = -1;
        }
    };
    if (material.pbr.has_value()) {
        glsl_material.base_color_factor = material.pbr->base_color_factor;
        glsl_material.metallic_factor = static_cast<float>(material.pbr->metallic_factor);
        glsl_material.roughness_factor = static_cast<float>(material.pbr->roughness_factor);

        LoadTexture(material.pbr->base_color_texture, glsl_material.base_color_texture_index,
                    glsl_material.base_color_texture_texcoord);
        LoadTexture(material.pbr->metallic_roughness_texture,
                    glsl_material.metallic_roughness_texture_index,
                    glsl_material.metallic_roughness_texture_texcoord);
    } else {
        glsl_material.base_color_factor = glm::vec4{1, 1, 1, 1};
        glsl_material.metallic_factor = 1.0;
        glsl_material.roughness_factor = 1.0;
        glsl_material.base_color_texture_index = -1;
        glsl_material.metallic_roughness_texture_index = -1;
    }

    LoadTexture(material.normal_texture, glsl_material.normal_texture_index,
                glsl_material.normal_texture_texcoord);
    if (material.normal_texture.has_value()) {
        glsl_material.normal_scale = static_cast<float>(material.normal_texture->scale);
    }

    LoadTexture(material.occlusion_texture, glsl_material.occlusion_texture_index,
                glsl_material.occlusion_texture_texcoord);
    if (material.occlusion_texture.has_value()) {
        glsl_material.occlusion_strength = static_cast<float>(material.occlusion_texture->strength);
    }

    LoadTexture(material.emissive_texture, glsl_material.emissive_texture_index,
                glsl_material.emissive_texture_texcoord);
    glsl_material.emissive_factor = material.emissive_factor;
}

Material::Material(std::string name, const GLSL::Material& glsl_material)
    : name(std::move(name)), glsl_material(glsl_material) {}

Material::~Material() = default;

MeshPrimitive::MeshPrimitive(const GLTF::Mesh::Primitive& primitive_) : primitive(primitive_) {}

MeshPrimitive::MeshPrimitive(SceneLoader& loader, const GLTF::Mesh::Primitive& primitive_)
    : primitive(primitive_) {
    if (primitive.material.has_value()) {
        material = static_cast<int>(loader.materials.GetIndex(loader, *primitive.material));
    }
    if (primitive.indices.has_value()) {
        index_buffer = loader.index_accessors.Get(loader, *primitive.indices);
    }
    if (primitive.attributes.color_0.has_value()) {
        color_is_vec4 = loader.gltf.accessors[*primitive.attributes.color_0].type == "VEC4";
    }

    const std::array<std::optional<std::size_t>, 6> attribute_accessors{{
        primitive.attributes.position,
        primitive.attributes.normal,
        primitive.attributes.tangent,
        primitive.attributes.texcoord_0,
        primitive.attributes.texcoord_1,
        primitive.attributes.color_0,
    }};
    for (const auto& accessor_idx : attribute_accessors) {
        if (!accessor_idx.has_value()) {
            continue;
        }

        const auto& accessor = loader.gltf.accessors[*accessor_idx];
        if (accessor.buffer_view.has_value()) {
            loader.vertex_buffer_views.Get(loader, *accessor.buffer_view)->AddAccessor(accessor);
        }
        if (max_vertices != 0 && max_vertices != accessor.count) {
            SPDLOG_ERROR("Different accessors in primitive should have the same count");
            throw std::runtime_error("Different accessors in primitive should have the same count");
        }
        max_vertices = accessor.count;
    }
}

MeshPrimitive::~MeshPrimitive() = default;

void MeshPrimitive::Load(SceneLoader& loader) {
    if (!attributes.empty()) {
        return;
    }

    // Keep note of all the buffers we'll use
    // (buffer, buffer_offset) -> binding
    std::map<std::pair<vk::Buffer, std::size_t>, u32> binding_index_map;
    const auto GetBindingIndex =
        [this, &binding_index_map](const VertexBufferView::BufferInfo& info, std::size_t stride) {
            if (!binding_index_map.count({**info.buffer, info.buffer_offset})) {
                bindings.emplace_back(vk::VertexInputBindingDescription2EXT{
                    .binding = static_cast<u32>(bindings.size()),
                    .stride = static_cast<u32>(stride),
                    .inputRate = vk::VertexInputRate::eVertex,
                    .divisor = 1,
                });
                raw_vertex_buffers.emplace_back(**info.buffer);
                vertex_buffer_offsets.emplace_back(info.buffer_offset);
                vertex_buffers.emplace_back(info.buffer);
                binding_index_map.emplace(std::make_pair(**info.buffer, info.buffer_offset),
                                          static_cast<u32>(bindings.size() - 1));
            }
            return binding_index_map.at({**info.buffer, info.buffer_offset});
        };

    // accessor, default format
    const std::array<std::pair<std::optional<std::size_t>, vk::Format>, 6> attribute_accessors{{
        {primitive.attributes.position, vk::Format::eR32G32B32Sfloat},
        {primitive.attributes.normal, vk::Format::eR32G32B32Sfloat},
        {primitive.attributes.texcoord_0, vk::Format::eR32G32Sfloat},
        {primitive.attributes.texcoord_1, vk::Format::eR32G32Sfloat},
        {primitive.attributes.color_0, vk::Format::eR32G32B32A32Sfloat},
        {primitive.attributes.tangent, vk::Format::eR32G32B32A32Sfloat},
    }};

    int null_binding_idx = -1;
    for (std::size_t i = 0; i < attribute_accessors.size(); ++i) {
        const auto& [accessor_idx, default_format] = attribute_accessors[i];
        const auto AddNullBinding = [this, &null_binding_idx, i, default_format = default_format] {
            if (null_binding_idx == -1) {
                // Create a null binding
                bindings.emplace_back(vk::VertexInputBindingDescription2EXT{
                    .binding = static_cast<u32>(bindings.size()),
                    .stride = 0,
                    .inputRate = vk::VertexInputRate::eVertex,
                    .divisor = 1,
                });
                raw_vertex_buffers.emplace_back(VK_NULL_HANDLE);
                vertex_buffer_offsets.emplace_back(0);
                null_binding_idx = static_cast<int>(bindings.size() - 1);
            }
            attributes.emplace_back(vk::VertexInputAttributeDescription2EXT{
                .location = static_cast<u32>(i),
                .binding = static_cast<u32>(null_binding_idx),
                .format = default_format,
                .offset = 0,
            });
        };

        if (!accessor_idx.has_value()) {
            AddNullBinding();
            continue;
        }

        const auto& accessor = loader.gltf.accessors[*accessor_idx];
        u32 binding{};
        u32 attribute_offset{};
        if (accessor.buffer_view.has_value()) {
            auto& vertex_buffer_view =
                loader.vertex_buffer_views.Get(loader, *accessor.buffer_view);
            vertex_buffer_view->Load(loader);

            const auto& info = vertex_buffer_view->GetAccessorBufferInfo(accessor);
            const auto stride =
                loader.gltf.buffer_views[*accessor.buffer_view].byte_stride.value_or(
                    GetComponentSize(accessor.component_type) *
                    GLTF::GetComponentCount(accessor.type));
            binding = GetBindingIndex(info, stride);
            attribute_offset = static_cast<u32>(info.attribute_offset);
        } else {
            AddNullBinding();
        }
        attributes.emplace_back(vk::VertexInputAttributeDescription2EXT{
            .location = static_cast<u32>(i),
            .binding = binding,
            .format = GLTF::GetVertexInputFormat(accessor.component_type, accessor.type,
                                                 accessor.normalized),
            .offset = attribute_offset,
        });
    }
}

CPUAccessor::CPUAccessor(SceneLoader& loader, const GLTF::Accessor& accessor) {
    data.resize(GetTotalSize(accessor));
    if (!accessor.buffer_view.has_value()) {
        return;
    }

    const auto& buffer_view = loader.gltf.buffer_views[*accessor.buffer_view];
    auto& buffer_file = *loader.buffer_files.Get(loader, buffer_view.buffer);
    if (buffer_view.byte_stride.has_value()) {
        const auto byte_stride = *buffer_view.byte_stride;
        const auto element_size =
            GetComponentSize(accessor.component_type) * GLTF::GetComponentCount(accessor.type);
        for (std::size_t i = 0; i < accessor.count; ++i) {
            buffer_file.Seek(buffer_view.byte_offset + accessor.byte_offset + i * byte_stride);
            buffer_file.Read(data.data() + i * element_size, element_size);
        }
    } else {
        buffer_file.Seek(buffer_view.byte_offset + accessor.byte_offset);
        buffer_file.Read(data.data(), data.size());
    }
}

CPUAccessor::~CPUAccessor() = default;

MeshPrimitiveGenerateTangent::MeshPrimitiveGenerateTangent(SceneLoader& loader,
                                                           const GLTF::Mesh::Primitive& primitive_)
    : MeshPrimitive(primitive_) {
    if (primitive.material.has_value()) {
        material = static_cast<int>(loader.materials.GetIndex(loader, *primitive.material));
    }
    color_is_vec4 = true;
}

MeshPrimitiveGenerateTangent::~MeshPrimitiveGenerateTangent() = default;

namespace MikkT {

struct Vertex {
    glm::vec3 position{};
    glm::vec3 normal{};
    glm::vec2 texcoord_0{};
    glm::vec2 texcoord_1{};
    glm::vec4 color{1.0f};
    glm::vec4 tangent{};

    bool operator==(const Vertex&) const = default;
};

} // namespace MikkT
} // namespace Renderer

template <>
struct std::hash<Renderer::MikkT::Vertex> {
    std::size_t operator()(const Renderer::MikkT::Vertex& v) const noexcept {
        return boost::pfr::hash_fields(v);
    }
};

namespace Renderer {
namespace MikkT {

struct UserData {
    const std::vector<Vertex>& vertices;
    const std::vector<u32>& indices;
    std::size_t tex_coord{};
    std::vector<glm::vec4> out;
};

static int GetNumFaces(const SMikkTSpaceContext* context) {
    const auto& data = *reinterpret_cast<UserData*>(context->m_pUserData);
    if (data.indices.empty()) {
        return static_cast<int>(data.vertices.size() / 3);
    }
    return static_cast<int>(data.indices.size() / 3);
}

static int GetVertexIndex(const UserData& data, int idx) {
    if (data.indices.empty()) {
        return idx;
    }
    return data.indices[idx];
}

static float LoadFloat(const std::vector<u8>& data, std::size_t idx,
                       GLTF::Accessor::ComponentType type) {
    if (type == GLTF::Accessor::ComponentType::Float) {
        return *reinterpret_cast<const float*>(data.data() + idx * sizeof(float));
    } else if (type == GLTF::Accessor::ComponentType::UnsignedByte) {
        return (*(data.data() + idx)) / 255.0f;
    } else if (type == GLTF::Accessor::ComponentType::UnsignedShort) {
        return (*reinterpret_cast<const u16_le*>(data.data() + idx * sizeof(u16))) / 65535.0f;
    }
    SPDLOG_ERROR("Invalid component type {}", static_cast<int>(type));
    throw std::runtime_error("Invalid component type");
}

static void GetPosition(const SMikkTSpaceContext* context, float out[], int face, int vert) {
    const auto& data = *reinterpret_cast<UserData*>(context->m_pUserData);
    const int idx = GetVertexIndex(data, face * 3 + vert);
    for (glm::length_t i = 0; i < 3; ++i) {
        out[i] = data.vertices[idx].position[i];
    }
}

static void GetNormal(const SMikkTSpaceContext* context, float out[], int face, int vert) {
    const auto& data = *reinterpret_cast<UserData*>(context->m_pUserData);
    const int idx = GetVertexIndex(data, face * 3 + vert);
    for (glm::length_t i = 0; i < 3; ++i) {
        out[i] = data.vertices[idx].normal[i];
    }
}

static void GetTexCoord(const SMikkTSpaceContext* context, float out[], int face, int vert) {
    const auto& data = *reinterpret_cast<UserData*>(context->m_pUserData);
    const int idx = GetVertexIndex(data, face * 3 + vert);
    for (glm::length_t i = 0; i < 2; ++i) {
        if (data.tex_coord == 0) {
            out[i] = data.vertices[idx].texcoord_0[i];
        } else {
            out[i] = data.vertices[idx].texcoord_1[i];
        }
    }
}

static void SetTSpace(const SMikkTSpaceContext* context, const float tangent[], float sign,
                      int face, int vert) {
    auto& data = *reinterpret_cast<UserData*>(context->m_pUserData);
    data.out[face * 3 + vert] = glm::vec4{tangent[0], tangent[1], tangent[2], -sign};
}

} // namespace MikkT

static bool ShouldGenerateTangent(SceneLoader& loader, const GLTF::Mesh::Primitive& primitive) {
    if (primitive.attributes.tangent.has_value()) {
        return false;
    }
    if (!primitive.attributes.position.has_value() || !primitive.attributes.normal.has_value() ||
        !primitive.material.has_value()) {
        return false;
    }

    const auto& info = loader.materials.Get(loader, *primitive.material).glsl_material;
    if (info.normal_texture_index == -1) {
        return false;
    }

    const auto& texcoord =
        info.normal_texture_texcoord == 0
            ? static_cast<const std::optional<std::size_t>&>(primitive.attributes.texcoord_0)
            : static_cast<const std::optional<std::size_t>&>(primitive.attributes.texcoord_1);
    if (!texcoord.has_value()) {
        return false;
    }
    return true;
}

template <glm::length_t L>
glm::vec<L, float> LoadVec(SceneLoader& loader, std::optional<std::size_t> accessor_idx,
                           std::size_t idx) {
    if (!accessor_idx.has_value()) {
        return glm::vec<L, float>{};
    }
    const auto& data = loader.cpu_accessors.Get(loader, *accessor_idx)->data;
    const auto component_type = loader.gltf.accessors[*accessor_idx].component_type;

    glm::vec<L, float> out;
    for (glm::length_t i = 0; i < L; ++i) {
        out[i] = MikkT::LoadFloat(data, idx * L + i, component_type);
    }
    return out;
}

void MeshPrimitiveGenerateTangent::Load(SceneLoader& loader) {
    SPDLOG_WARN("Generating tangents");

    // Load vertex data to CPU
    max_vertices = loader.gltf.accessors[*primitive.attributes.position].count;
    std::vector<MikkT::Vertex> old_vertices(max_vertices);
    for (std::size_t i = 0; i < old_vertices.size(); ++i) {
        old_vertices[i] = {
            .position = LoadVec<3>(loader, primitive.attributes.position, i),
            .normal = LoadVec<3>(loader, primitive.attributes.normal, i),
            .texcoord_0 = LoadVec<2>(loader, primitive.attributes.texcoord_0, i),
            .texcoord_1 = LoadVec<2>(loader, primitive.attributes.texcoord_1, i),
        };
        if (!primitive.attributes.color_0.has_value()) {
            continue;
        }
        const auto& accessor = loader.gltf.accessors[*primitive.attributes.color_0];
        if (accessor.type == "VEC3") {
            old_vertices[i].color = {LoadVec<3>(loader, primitive.attributes.color_0, i), 1.0f};
        } else if (accessor.type == "VEC4") {
            old_vertices[i].color = LoadVec<4>(loader, primitive.attributes.color_0, i);
        } else {
            SPDLOG_ERROR("Invalid accessor type {} for color", accessor.type);
            throw std::runtime_error("Invalid accessor type for color");
        }
    }

    // Load index data to CPU
    std::vector<u32> old_indices;
    if (primitive.indices.has_value()) {
        const auto& index_data = loader.cpu_accessors.Get(loader, *primitive.indices)->data;
        const auto& accessor = loader.gltf.accessors[*primitive.indices];
        old_indices.resize(accessor.count);
        for (std::size_t i = 0; i < accessor.count; ++i) {
            if (accessor.component_type == GLTF::Accessor::ComponentType::UnsignedByte) {
                old_indices[i] = index_data[i];
            } else if (accessor.component_type == GLTF::Accessor::ComponentType::UnsignedShort) {
                old_indices[i] = index_data[2 * i] + (index_data[2 * i + 1] << static_cast<u16>(8));
            } else if (accessor.component_type == GLTF::Accessor::ComponentType::UnsignedInt) {
                old_indices[i] = index_data[4 * i] +
                                 (index_data[4 * i + 1] << static_cast<u32>(8)) +
                                 (index_data[4 * i + 2] << static_cast<u32>(16)) +
                                 (index_data[4 * i + 3] << static_cast<u32>(24));
            } else {
                SPDLOG_ERROR(
                    "Invalid component type for indices {}",
                    static_cast<int>(GLTF::Accessor::ComponentType{accessor.component_type}));
                throw std::runtime_error("Invalid component type for indices");
            }
        }
    }

    MikkT::UserData user_data{
        .vertices = old_vertices,
        .indices = old_indices,
        .tex_coord =
            loader.materials.Get(loader, *primitive.material).glsl_material.normal_texture_texcoord,
    };
    const std::size_t total_vertices =
        user_data.indices.empty() ? max_vertices : user_data.indices.size();
    user_data.out.resize(total_vertices);

    SMikkTSpaceInterface callbacks{
        .m_getNumFaces = &MikkT::GetNumFaces,
        .m_getNumVerticesOfFace = [](const SMikkTSpaceContext*, int) { return 3; },
        .m_getPosition = &MikkT::GetPosition,
        .m_getNormal = &MikkT::GetNormal,
        .m_getTexCoord = &MikkT::GetTexCoord,
        .m_setTSpaceBasic = &MikkT::SetTSpace,
    };
    SMikkTSpaceContext context{
        .m_pInterface = &callbacks,
        .m_pUserData = &user_data,
    };
    if (!genTangSpaceDefault(&context)) {
        SPDLOG_ERROR("Failed to generate tangent space");
        throw std::runtime_error("Failed to generate tangent space");
    }

    // Reindex vertices
    std::vector<MikkT::Vertex> vertices;
    std::vector<u32_le> indices;
    std::unordered_map<MikkT::Vertex, u32> vertex_index_map;
    for (std::size_t i = 0; i < total_vertices; ++i) {
        auto vertex = old_vertices.at(MikkT::GetVertexIndex(user_data, static_cast<int>(i)));
        vertex.tangent = user_data.out[i];

        if (!vertex_index_map.count(vertex)) {
            vertices.emplace_back(vertex);
            vertex_index_map.emplace(std::move(vertex), static_cast<u32>(vertices.size() - 1));
        }
        indices.emplace_back(vertex_index_map.at(vertex));
    }

    // Upload vertices & indices
    vertex_buffers = {{std::make_shared<VulkanImmUploadBuffer>(
        loader.device,
        VulkanBufferCreateInfo{
            .size = vertices.size() * sizeof(MikkT::Vertex),
            .usage = loader.vertex_buffer_params.usage,
            .dst_stage_mask = loader.vertex_buffer_params.dst_stage_mask,
            .dst_access_mask = loader.vertex_buffer_params.dst_access_mask,
        },
        reinterpret_cast<const u8*>(vertices.data()))}};

    static constexpr auto VertexAttributes = Helpers::AttributeDescriptionsFor<MikkT::Vertex>();
    attributes.assign(VertexAttributes.begin(), VertexAttributes.end());

    bindings = {{
        .binding = 0,
        .stride = sizeof(MikkT::Vertex),
        .inputRate = vk::VertexInputRate::eVertex,
        .divisor = 1,
    }};
    raw_vertex_buffers = {{**vertex_buffers[0]}};
    vertex_buffer_offsets = {{0}};

    index_buffer = std::make_shared<IndexBufferAccessor>();
    index_buffer->name = "GeneratedIndexBuffer";
    index_buffer->gpu_buffer = std::make_shared<VulkanImmUploadBuffer>(
        loader.device,
        VulkanBufferCreateInfo{
            .size = indices.size() * sizeof(u32_le),
            .usage = loader.index_buffer_params.usage,
            .dst_stage_mask = loader.index_buffer_params.dst_stage_mask,
            .dst_access_mask = loader.index_buffer_params.dst_access_mask,
        },
        reinterpret_cast<const u8*>(indices.data()));
    index_buffer->component_type = GLTF::Accessor::ComponentType::UnsignedInt;
    index_buffer->type = "SCALAR";
    index_buffer->count = indices.size();
}

Mesh::Mesh(SceneLoader& loader, const GLTF::Mesh& mesh) : name(mesh.name.value_or("Unnamed")) {
    primitives = Common::VectorFromRange(
        mesh.primitives |
        std::views::transform(
            [&loader](const GLTF::Mesh::Primitive& primitive) -> std::unique_ptr<MeshPrimitive> {
                if (ShouldGenerateTangent(loader, primitive)) {
                    return std::make_unique<MeshPrimitiveGenerateTangent>(loader, primitive);
                } else {
                    return std::make_unique<MeshPrimitive>(loader, primitive);
                }
            }));
}

Mesh::~Mesh() = default;

void Mesh::Load(SceneLoader& loader) {
    for (auto& primitive : primitives) {
        primitive->Load(loader);
    }
}

Camera::Camera(const glm::vec3& position, const glm::vec3& front, const glm::vec3& up) {
    camera.perspective = {GLTF::Camera::Perspective{
        .yfov = {glm::radians(45.0f)},
        .znear = {0.01},
    }};
    view = glm::lookAt(position, position + front, up);
}

Camera::Camera([[maybe_unused]] const SceneLoader& loader, const GLTF::Camera& camera_,
               const glm::mat4& transform)
    : name(camera_.name.value_or("Unnamed")), camera(camera_) {
    view = glm::lookAt(glm::vec3{transform[3]}, glm::vec3{transform[3] - transform[2]},
                       glm::normalize(glm::vec3{transform[1]}));
}

Camera::~Camera() = default;

glm::mat4 Camera::GetProj(double default_aspect_ratio) const {
    glm::mat4 proj;
    if (camera.perspective.has_value()) {
        const double aspect_ratio = camera.perspective->aspect_ratio.value_or(default_aspect_ratio);
        if (camera.perspective->zfar.has_value()) {
            proj = glm::perspective<float>(static_cast<float>(camera.perspective->yfov),
                                           static_cast<float>(aspect_ratio),
                                           static_cast<float>(camera.perspective->znear),
                                           static_cast<float>(*camera.perspective->zfar));
        } else {
            proj = glm::infinitePerspective<float>(static_cast<float>(camera.perspective->yfov),
                                                   static_cast<float>(aspect_ratio),
                                                   static_cast<float>(camera.perspective->znear));
        }
    } else if (camera.orthographic.has_value()) {
        proj = glm::ortho<float>(static_cast<float>(-camera.orthographic->xmag),
                                 static_cast<float>(camera.orthographic->xmag),
                                 static_cast<float>(-camera.orthographic->ymag),
                                 static_cast<float>(camera.orthographic->ymag),
                                 static_cast<float>(camera.orthographic->znear),
                                 static_cast<float>(camera.orthographic->zfar));
    } else {
        SPDLOG_ERROR("Camera {} has neither perspective nor orthographic", name);
        throw std::runtime_error("Camera has neither perspective nor orthographic");
    }
    proj[1][1] *= -1;
    return proj;
}

double Camera::GetAspectRatio(double default_aspect_ratio) const {
    if (camera.perspective.has_value()) {
        return camera.perspective->aspect_ratio.value_or(default_aspect_ratio);
    } else if (camera.orthographic.has_value()) {
        return camera.orthographic->xmag / camera.orthographic->ymag;
    } else {
        SPDLOG_ERROR("Camera {} has neither perspective nor orthographic", name);
        throw std::runtime_error("Camera has neither perspective nor orthographic");
    }
}

SubScene::SubScene(SceneLoader& loader, const GLTF::Scene& scene)
    : name(scene.name.value_or("Unnamed")) {
    for (const std::size_t node : scene.nodes) {
        VisitNode(loader, node, glm::mat4{1});
    }
    visited_nodes.clear();
}

SubScene::~SubScene() = default;

void SubScene::VisitNode(SceneLoader& loader, std::size_t node_idx, glm::mat4 parent_transform) {
    if (visited_nodes.count(node_idx)) {
        SPDLOG_ERROR("Nodes formed a cycle");
        throw std::runtime_error("Nodes formed a cycle");
    }
    visited_nodes.emplace(node_idx);

    const auto& node = loader.gltf.nodes[node_idx];

    auto transform = parent_transform;
    if (node.matrix.has_value()) {
        transform = transform * (*node.matrix);
    } else {
        if (node.translation.has_value()) {
            transform = glm::translate(transform, (*node.translation));
        }
        if (node.rotation.has_value()) {
            transform = transform * glm::mat4_cast(glm::quat{*node.rotation});
        }
        if (node.scale.has_value()) {
            transform = glm::scale(transform, (*node.scale));
        }
    }
    if (node.camera) {
        cameras.emplace_back(
            std::make_unique<Camera>(loader, loader.gltf.cameras[*node.camera], transform));
    }
    if (node.mesh) {
        mesh_instances.emplace_back(loader.meshes.Get(loader, *node.mesh), transform);
    }
    for (const std::size_t child : node.children) {
        VisitNode(loader, child, transform);
    }
}

void SubScene::Load(SceneLoader& loader) {
    for (const auto& [mesh, _] : mesh_instances) {
        mesh->Load(loader);
    }
}

static std::pair<long, long> ParseVersion(const std::string_view& str) {
    const auto pos = str.find('.');
    if (pos == std::string_view::npos) {
        SPDLOG_ERROR("Version invalid {}", str);
        throw std::runtime_error("Version is invalid");
    }
    return {std::stoi(std::string{str.substr(0, pos)}),
            std::stoi(std::string{str.substr(pos + 1)})};
}

SceneLoader::SceneLoader(const BufferParams& vertex_buffer_params_,
                         const BufferParams& index_buffer_params_, Scene& scene_,
                         VulkanDevice& device_, GLTF::Container& container_)
    : vertex_buffer_params(vertex_buffer_params_), index_buffer_params(index_buffer_params_),
      scene(scene_), device(device_), container(container_) {

    const auto prev_current_path = std::filesystem::current_path();
    if (container.path.has_parent_path()) {
        std::filesystem::current_path(container.path.parent_path());
    }
    SCOPE_EXIT({ std::filesystem::current_path(prev_current_path); });

    gltf = JSON::Deserialize<GLTF::GLTF>(container.json.get_value());

    // Check GLTF version
    if (gltf.asset.min_version.has_value()) {
        const auto [major, minor] = ParseVersion(*gltf.asset.min_version);
        if (major != GLTF::MajorVersion ||
            (major == GLTF::MajorVersion && minor > GLTF::MinorVersion)) {
            SPDLOG_ERROR("Version is unsupported (minVersion={}, supported={}.{})",
                         *gltf.asset.min_version, GLTF::MajorVersion, GLTF::MinorVersion);
            throw std::runtime_error("Version unsupported");
        }
    } else {
        const auto [major, minor] = ParseVersion(gltf.asset.version);
        if (major != GLTF::MajorVersion) {
            SPDLOG_ERROR("Version is unsupported (version={}, supported={}.{})", gltf.asset.version,
                         GLTF::MajorVersion, GLTF::MinorVersion);
            throw std::runtime_error("Version unsupported");
        }
    }

    if (gltf.scene.has_value()) {
        scene.main_sub_scene = std::make_unique<SubScene>(*this, gltf.scenes[*gltf.scene]);
        scene.main_sub_scene->Load(*this);
    } else {
        SPDLOG_ERROR("No main scene in glTF");
        throw std::runtime_error("No main scene in glTF");
    }

    if (scene.main_sub_scene->cameras.empty()) {
        SPDLOG_WARN("No camera in main scene, external camera will be used");
    }

    // Add a default material
    scene.materials.emplace_back(
        std::make_unique<Material>("Default", GLSL::Material{
                                                  .base_color_factor = glm::vec4{1, 1, 1, 1},
                                                  .base_color_texture_index = -1,
                                                  .metallic_factor = 1.0,
                                                  .roughness_factor = 1.0,
                                                  .metallic_roughness_texture_index = -1,
                                                  .normal_texture_index = -1,
                                                  .occlusion_texture_index = -1,
                                                  .emissive_texture_index = -1,
                                                  .emissive_factor = glm::vec3{},
                                              }));
}

SceneLoader::~SceneLoader() = default;

} // namespace Renderer
