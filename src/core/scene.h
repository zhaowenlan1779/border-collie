// Copyright 2023 Pengfei Zhu
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <memory>
#include <utility>
#include <vector>
#include <glm/glm.hpp>
#include <vulkan/vulkan_raii.hpp>
#include "core/gltf/gltf.h"
#include "core/gltf/json_helpers.hpp"
#include "core/gltf/simdjson.h"
#include "core/shaders/scene_glsl.h"

namespace Renderer {

class VulkanDevice;
class VulkanTexture;

struct Sampler {
    std::string name;
    bool uses_mipmaps{};
    vk::raii::Sampler sampler = nullptr;
};

struct Scene {
    std::vector<std::unique_ptr<VulkanTexture>> textures;
    std::vector<Sampler> samplers;
};

class SceneLoader {
public:
    explicit SceneLoader(Scene& scene, VulkanDevice& device, simdjson::ondemand::document& json);

    // Load basic information about buffers, buffer views, accessors, etc.
    // The actual data will be read and uploaded in Finalize.
    void Init();

    void Load();

    // Upload the buffers and images that are actually referenced.
    void Finalize();

protected:
    // Marks that an accessor is actually used and should be loaded
    void MarkAccessorUsed(std::size_t idx);

    ~SceneLoader();

    Scene& scene;
    VulkanDevice& device;
    simdjson::ondemand::document& json;
    GLTF::GLTF gltf;
};

} // namespace Renderer
