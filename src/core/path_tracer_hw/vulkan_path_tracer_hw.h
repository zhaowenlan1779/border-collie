// Copyright 2022 Pengfei Zhu
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <memory>
#include <utility>
#include <vector>
#include <vulkan/vulkan_raii.hpp>
#include "core/vulkan_renderer.h"

namespace Renderer {

class VulkanAccelStructure;
class VulkanImmUploadBuffer;
class VulkanTexture;
class VulkanRayTracingPipeline;
class VulkanDescriptorSets;

class VulkanPathTracerHW : public VulkanRenderer {
public:
    explicit VulkanPathTracerHW(bool enable_validation_layers,
                                std::vector<const char*> frontend_required_extensions);
    ~VulkanPathTracerHW() override;

    void Init(vk::SurfaceKHR surface, const vk::Extent2D& actual_extent) override;
    void LoadScene(GLTF::Container& gltf) override;
    void DrawFrame(const Camera& external_camera, bool force_external_camera) override;
    void OnResized(const vk::Extent2D& actual_extent) override;
    void SetLightProperties(float multiplier, float ambient_light);

private:
    OffscreenImageInfo GetOffscreenImageInfo() const override;
    std::unique_ptr<VulkanDevice> CreateDevice(vk::SurfaceKHR surface,
                                               const vk::Extent2D& actual_extent) const override;

    std::unique_ptr<VulkanImmUploadBuffer> primitives_buffer;
    std::unique_ptr<VulkanImmUploadBuffer> materials_buffer;
    std::unique_ptr<VulkanTexture> error_texture;
    std::vector<std::unique_ptr<VulkanAccelStructure>> blases;
    std::unique_ptr<VulkanAccelStructure> tlas;

    struct Frame {};
    std::unique_ptr<VulkanFramesInFlight<Frame, 2>> frames;
    std::unique_ptr<VulkanDescriptorSets> fixed_descriptor_set;
    std::unique_ptr<VulkanDescriptorSets> image_descriptor_sets;
    std::unique_ptr<VulkanRayTracingPipeline> pipeline;

    u32 frame_count = 0;
    glm::mat4 last_camera_view;
    glm::mat4 last_camera_proj;
    float intensity_multiplier = 20.0;
    float ambient_light = 5.0;
};

} // namespace Renderer
