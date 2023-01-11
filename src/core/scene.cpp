// Copyright 2023 Pengfei Zhu
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <map>
#include <ranges>
#include <type_traits>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <libbase64.h>
#include <spdlog/spdlog.h>
#include "common/assert.h"
#include "common/ranges.h"
#include "core/gltf/json_helpers.hpp"
#include "core/scene.h"
#include "core/vulkan/vulkan_buffer.h"
#include "core/vulkan/vulkan_device.h"
#include "core/vulkan/vulkan_texture.h"

namespace Renderer {

BufferFile::BufferFile(const std::string_view& uri) {
    Load(uri);
}
BufferFile::BufferFile([[maybe_unused]] const SceneLoader& loader, const GLTF::Buffer& buffer) {
    Load(*buffer.uri);
}
BufferFile::~BufferFile() = default;

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
        // Open file
        file = std::ifstream(std::filesystem::u8path(uri), std::ios::ate | std::ios::binary);
        if (!file) {
            SPDLOG_ERROR("Failed to open file {}", uri);
            throw std::runtime_error("Failed to open file");
        }
        file_size = file.tellg();
        file.seekg(0);
    }
}

void BufferFile::Read(void* out, std::size_t size) {
    if (file) {
        file.read(reinterpret_cast<char*>(out), size);
        if (!file) {
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
        file.seekg(new_pos);
        if (!file) {
            SPDLOG_ERROR("Failed to seek file");
            throw std::runtime_error("Failed to seek file");
        }
    } else {
        pos = new_pos;
    }
}

CPUAccessor::CPUAccessor(SceneLoader& loader, const GLTF::Accessor& accessor)
    : name(accessor.name.value_or("Unnamed")) {

    const auto total_size = GetComponentSize(accessor.component_type) *
                            GLTF::GetComponentCount(accessor.type) * accessor.count;
    data.resize(total_size);

    if (accessor.buffer_view.has_value()) {
        const auto& buffer_view = loader.gltf.buffer_views[*accessor.buffer_view];
        auto& buffer_file = *loader.buffer_files.Get(loader, buffer_view.buffer);
        buffer_file.Seek(accessor.byte_offset + buffer_view.byte_offset);
        buffer_file.Read(data.data(), data.size());
    }
}

CPUAccessor::~CPUAccessor() = default;

GPUAccessor::GPUAccessor(SceneLoader& loader, const GLTF::Accessor& accessor,
                         const BufferParams& params)
    : name(accessor.name.value_or("Unnamed")) {

    const auto total_size = GetComponentSize(accessor.component_type) *
                            GLTF::GetComponentCount(accessor.type) * accessor.count;
    if (accessor.buffer_view.has_value()) {
        const auto& buffer_view = loader.gltf.buffer_views[*accessor.buffer_view];
        auto& buffer_file = *loader.buffer_files.Get(loader, buffer_view.buffer);
        buffer_file.Seek(accessor.byte_offset + buffer_view.byte_offset);

        gpu_buffer = std::make_unique<VulkanImmUploadBuffer>(
            loader.device,
            VulkanBufferCreateInfo{total_size, params.usage, params.dst_stage_mask,
                                   params.dst_access_mask},
            [&buffer_file](void* data, std::size_t size) { buffer_file.Read(data, size); });
    } else {
        gpu_buffer = std::make_unique<VulkanZeroedBuffer>(
            loader.device, VulkanBufferCreateInfo{total_size, params.usage, params.dst_stage_mask,
                                                  params.dst_access_mask});
    }
}

GPUAccessor::~GPUAccessor() = default;

StridedBufferView::StridedBufferView([[maybe_unused]] const SceneLoader& loader,
                                     const GLTF::BufferView& buffer_view_)
    : buffer_view(buffer_view_) {

    ASSERT(buffer_view.byte_stride.has_value());
}

StridedBufferView::~StridedBufferView() = default;

void StridedBufferView::AddAccessor(const GLTF::Accessor& accessor) {
    const auto start = accessor.byte_offset / (*buffer_view.byte_stride);
    const auto attribute_offset = accessor.byte_offset % (*buffer_view.byte_stride);
    chunks.insert(boost::icl::interval<std::size_t>::right_open(start, start + accessor.count));
}

void StridedBufferView::Load(SceneLoader& loader) {
    if (!buffers.empty()) {
        return;
    }

    const auto byte_stride = *buffer_view.byte_stride;

    auto& buffer_file = *loader.buffer_files.Get(loader, buffer_view.buffer);
    for (const auto& chunk : chunks) {
        buffer_file.Seek(buffer_view.byte_offset + chunk.lower() * byte_stride);
        buffers.emplace(
            chunk.lower(),
            std::make_unique<VulkanImmUploadBuffer>(
                loader.device,
                VulkanBufferCreateInfo{
                    .size = (chunk.upper() - chunk.lower()) * byte_stride,
                    .usage = loader.vertex_buffer_params.usage,
                    .dst_stage_mask = loader.vertex_buffer_params.dst_stage_mask,
                    .dst_access_mask = loader.vertex_buffer_params.dst_access_mask,
                },
                [&buffer_file](void* data, std::size_t size) { buffer_file.Read(data, size); }));
    }
}

StridedBufferView::BufferInfo StridedBufferView::GetAccessorBufferInfo(
    const GLTF::Accessor& accessor) const {

    const auto byte_stride = *buffer_view.byte_stride;
    const auto start = accessor.byte_offset / byte_stride;
    const auto attribute_offset = accessor.byte_offset % byte_stride;

    const auto iter = chunks.find(start);
    ASSERT_MSG(iter != chunks.end(), "Failed to find interval containing accessor");
    return {
        .buffer = *buffers.at(iter->lower()),
        .buffer_offset = buffer_view.byte_offset + (start - iter->lower()) * byte_stride,
        .attribute_offset = attribute_offset,
    };
}

Sampler::Sampler(const SceneLoader& loader)
    : name("Default"),
      uses_mipmaps(true), sampler{*loader.device,
                                  vk::SamplerCreateInfo{
                                      .magFilter = vk::Filter::eLinear,
                                      .minFilter = vk::Filter::eLinear,
                                      .mipmapMode = vk::SamplerMipmapMode::eLinear,
                                      .addressModeU = vk::SamplerAddressMode::eRepeat,
                                      .addressModeV = vk::SamplerAddressMode::eRepeat,
                                      .mipLodBias = 0.0f,
                                      .anisotropyEnable = VK_TRUE,
                                      .maxAnisotropy = loader.device.physical_device.getProperties()
                                                           .limits.maxSamplerAnisotropy,
                                      .minLod = 0.0f,
                                      .maxLod = 0.0f,
                                      .borderColor = vk::BorderColor::eIntOpaqueBlack,
                                  }} {}

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
    : name(texture.name.value_or("Unnamed")), image(loader.images.Get(loader, texture.source)),
      sampler(texture.sampler.has_value() ? loader.samplers.Get(loader, *texture.sampler)
                                          : loader.scene.default_sampler) {}
Texture::~Texture() = default;

Material::Material(SceneLoader& loader, const GLTF::Material& material)
    : name(material.name.value_or("Unnamed")) {

    const auto LoadTexture = [&loader](const auto& json_field, int& index, u32& texcoord) {
        if (json_field.has_value()) {
            index = loader.textures.GetIndex(loader, json_field->index);
            texcoord = json_field->texcoord;
        } else {
            index = -1;
        }
    };
    if (material.pbr.has_value()) {
        glsl_material.base_color_factor = material.pbr->base_color_factor;
        glsl_material.metallic_factor = material.pbr->metallic_factor;
        glsl_material.roughness_factor = material.pbr->roughness_factor;

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
        glsl_material.normal_scale = material.normal_texture->scale;
    }

    LoadTexture(material.occlusion_texture, glsl_material.occlusion_texture_index,
                glsl_material.occlusion_texture_texcoord);
    if (material.occlusion_texture.has_value()) {
        glsl_material.occlusion_strength = material.occlusion_texture->strength;
    }

    glsl_material.emissive_factor = material.emissive_factor;
}

Material::~Material() = default;

MeshPrimitive::MeshPrimitive(SceneLoader& loader, const GLTF::Mesh::Primitive& primitive_)
    : primitive(primitive_) {
    if (primitive.material.has_value()) {
        material = static_cast<int>(loader.materials.GetIndex(loader, *primitive.material));
    }
    if (primitive.indices.has_value()) {
        index_buffer =
            loader.gpu_accessors.Get(loader, *primitive.indices, loader.index_buffer_params);
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
        if (accessor.buffer_view.has_value() &&
            loader.gltf.buffer_views[*accessor.buffer_view].byte_stride.has_value()) {
            loader.strided_buffer_views.Get(loader, *accessor.buffer_view)->AddAccessor(accessor);
        }
    }
}

MeshPrimitive::~MeshPrimitive() = default;

void MeshPrimitive::Load(SceneLoader& loader) {
    if (!attributes.empty()) {
        return;
    }

    // Keep note of all the buffers we'll use
    std::map<vk::Buffer, u32> binding_index_map;
    const auto GetBindingIndex =
        [this, &binding_index_map](const StridedBufferView::BufferInfo& info, std::size_t stride) {
            if (!binding_index_map.count(*info.buffer)) {
                bindings.emplace_back(VertexInputBinding{
                    .description =
                        {
                            .binding = static_cast<u32>(bindings.size()),
                            .stride = static_cast<u32>(stride),
                            .inputRate = vk::VertexInputRate::eVertex,
                        },
                    .buffer = *info.buffer,
                    .offset = info.buffer_offset,
                });
                binding_index_map.emplace(*info.buffer, static_cast<u32>(bindings.size() - 1));
            }
            return binding_index_map.at(*info.buffer);
        };

    const std::array<std::optional<std::size_t>, 5> attribute_accessors{{
        primitive.attributes.position,
        primitive.attributes.normal,
        primitive.attributes.texcoord_0,
        primitive.attributes.texcoord_1,
        primitive.attributes.color_0,
    }};
    for (std::size_t i = 0; i < attribute_accessors.size(); ++i) {
        const auto& accessor_idx = attribute_accessors[i];
        if (!accessor_idx.has_value()) {
            continue;
        }

        const auto& accessor = loader.gltf.accessors[*accessor_idx];
        u32 binding{};
        u32 attribute_offset{};
        if (accessor.buffer_view.has_value() &&
            loader.gltf.buffer_views[*accessor.buffer_view].byte_stride.has_value()) {

            auto& strided_buffer_view =
                loader.strided_buffer_views.Get(loader, *accessor.buffer_view);
            strided_buffer_view->Load(loader);

            const auto& info = strided_buffer_view->GetAccessorBufferInfo(accessor);
            binding =
                GetBindingIndex(info, *loader.gltf.buffer_views[*accessor.buffer_view].byte_stride);
            attribute_offset = static_cast<u32>(info.attribute_offset);
        } else {
            binding = GetBindingIndex(
                {
                    .buffer = *loader.gpu_accessors
                                   .Get(loader, *accessor_idx, loader.vertex_buffer_params)
                                   ->gpu_buffer,
                    .buffer_offset = 0,
                    .attribute_offset = 0,
                },
                GetComponentSize(accessor.component_type) * GLTF::GetComponentCount(accessor.type));
        }
        attributes.emplace_back(vk::VertexInputAttributeDescription{
            .location = static_cast<u32>(i),
            .binding = binding,
            .format = GLTF::GetVertexInputFormat(accessor.component_type, accessor.type,
                                                 accessor.normalized),
            .offset = attribute_offset,
        });
    }
}

Mesh::Mesh(SceneLoader& loader, const GLTF::Mesh& mesh) : name(mesh.name.value_or("Unnamed")) {
    primitives = Common::VectorFromRange(
        mesh.primitives | std::views::transform([&loader](const GLTF::Mesh::Primitive& primitive) {
            return std::make_unique<MeshPrimitive>(loader, primitive);
        }));
}

Mesh::~Mesh() = default;

void Mesh::Load(SceneLoader& loader) {
    for (auto& primitive : primitives) {
        primitive->Load(loader);
    }
}

Camera::Camera(SceneLoader& loader, const GLTF::Camera& camera_, glm::mat4 transform_)
    : name(camera_.name.value_or("Unnamed")), transform(std::move(transform_)), camera(camera_) {
    view = glm::lookAt(glm::vec3{transform[3]}, glm::vec3{transform[3] - transform[2]},
                       glm::normalize(glm::vec3{transform[1]}));
}

Camera::~Camera() = default;

glm::mat4 Camera::GetProj(double default_aspect_ratio) const {
    if (camera.perspective.has_value()) {
        const double aspect_ratio = camera.perspective->aspect_ratio.has_value()
                                        ? *camera.perspective->aspect_ratio
                                        : default_aspect_ratio;
        if (camera.perspective->zfar.has_value()) {
            return glm::perspective<float>(camera.perspective->yfov, aspect_ratio,
                                           camera.perspective->znear, *camera.perspective->zfar);
        } else {
            return glm::infinitePerspective<float>(camera.perspective->yfov, aspect_ratio,
                                                   camera.perspective->znear);
        }
    } else if (camera.orthographic.has_value()) {
        return glm::ortho<float>(-camera.orthographic->xmag, camera.orthographic->xmag,
                                 -camera.orthographic->ymag, camera.orthographic->ymag,
                                 camera.orthographic->znear, camera.orthographic->zfar);
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
                         VulkanDevice& device_, simdjson::ondemand::document& json_)
    : vertex_buffer_params(vertex_buffer_params_), index_buffer_params(index_buffer_params_),
      scene(scene_), device(device_), json(json_) {

    gltf = JSON::Deserialize<GLTF::GLTF>(json.get_value());

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

    scene.default_sampler = std::make_unique<Sampler>(*this);

    // Load scenes
    for (const auto& sub_scene : gltf.scenes) {
        scene.sub_scenes.emplace_back(std::make_unique<SubScene>(*this, sub_scene));
    }
    for (const auto& sub_scene : scene.sub_scenes) {
        sub_scene->Load(*this);
    }
    if (gltf.scene.has_value()) {
        scene.main_sub_scene = scene.sub_scenes.at(*gltf.scene).get();
    }
}

SceneLoader::~SceneLoader() = default;

} // namespace Renderer
