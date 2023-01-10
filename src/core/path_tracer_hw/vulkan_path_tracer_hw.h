// Copyright 2022 Pengfei Zhu
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <memory>
#include <utility>
#include <vector>
#include <glm/glm.hpp>
#include <vulkan/vulkan_raii.hpp>
#include "core/vulkan_renderer.h"

namespace Renderer {

class VulkanAccelStructure;
class VulkanImmUploadBuffer;
class VulkanTexture;
class VulkanRayTracingPipeline;
template <typename T>
class VulkanUniformBufferObject;
namespace GLSL {
struct PathTracerUBOBlock;
}

class VulkanPathTracerHW : public VulkanRenderer {
public:
    explicit VulkanPathTracerHW(bool enable_validation_layers,
                                std::vector<const char*> frontend_required_extensions);
    ~VulkanPathTracerHW() override;

    void Init(vk::SurfaceKHR surface, const vk::Extent2D& actual_extent) override;
    void DrawFrame() override;
    void OnResized(const vk::Extent2D& actual_extent) override;

private:
    OffscreenImageInfo GetOffscreenImageInfo() const override;
    std::unique_ptr<VulkanDevice> CreateDevice(vk::SurfaceKHR surface,
                                               const vk::Extent2D& actual_extent) const override;
    GLSL::PathTracerUBOBlock GetUniformBufferObject() const;

    std::unique_ptr<VulkanImmUploadBuffer> vertex_buffer{};
    std::unique_ptr<VulkanImmUploadBuffer> index_buffer{};
    std::vector<std::unique_ptr<VulkanAccelStructure>> blas;
    std::unique_ptr<VulkanAccelStructure> tlas;
    std::unique_ptr<VulkanTexture> texture{};

    struct Frame {
        std::unique_ptr<VulkanUniformBufferObject<GLSL::PathTracerUBOBlock>> uniform{};
    };
    std::unique_ptr<VulkanFramesInFlight<Frame, 2>> frames;
    std::unique_ptr<VulkanRayTracingPipeline> pipeline;
};

} // namespace Renderer