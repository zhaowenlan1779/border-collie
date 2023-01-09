// Copyright 2023 Pengfei Zhu
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <set>
#include <type_traits>
#include "core/scene.h"
#include "core/vulkan/vulkan_device.h"

namespace Renderer {

SceneLoader::SceneLoader(Scene& scene_, VulkanDevice& device_, simdjson::ondemand::document& json_)
    : scene(scene_), device(device_), json(json_) {}

SceneLoader::~SceneLoader() = default;

void SceneLoader::Init() {
    gltf = JSON::Deserialize<GLTF::GLTF>(json.get_value());
}

void SceneLoader::Load() {
    // Build the node hierarchy
    std::vector<std::size_t> parent(gltf.nodes.size());
    for (std::size_t i = 0; i < gltf.nodes.size(); ++i) {
        for (const auto child : gltf.nodes[i].children) {
            parent[child] = i;
        }
    }

    std::set<std::size_t> referenced_accessors;
    for (const auto& mesh : gltf.meshes) {
    }

    // Samplers
    scene.samplers.clear();
    for (const auto& sampler : gltf.samplers) {
        // This is needed multiple times
        scene.samplers.emplace_back(Sampler{
            .name = std::string{sampler.name.value_or("")},
            .uses_mipmaps = IsMipmapUsed(sampler.min_filter),
            .sampler = {*device,
                        vk::SamplerCreateInfo{
                            .magFilter = ToVkFilter(sampler.mag_filter),
                            .minFilter = ToVkFilter(sampler.min_filter),
                            .mipmapMode = GetMipmapMode(sampler.min_filter),
                            .addressModeU = ToAddressMode(sampler.wrapS),
                            .addressModeV = ToAddressMode(sampler.wrapT),
                            .mipLodBias = 0.0f,
                            .anisotropyEnable = VK_TRUE,
                            .maxAnisotropy =
                                device.physical_device.getProperties().limits.maxSamplerAnisotropy,
                            .minLod = 0.0f,
                            .maxLod = 0.0f,
                            .borderColor = vk::BorderColor::eIntOpaqueBlack,
                        }},
        });
    }
}

} // namespace Renderer
