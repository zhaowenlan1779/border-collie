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

class VulkanGraphicsPipeline;
class VulkanImmUploadBuffer;
class VulkanTexture;

template <typename T>
class VulkanUniformBufferObject;

class VulkanRasterizer final : public VulkanRenderer {
public:
    explicit VulkanRasterizer(bool enable_validation_layers,
                              std::vector<const char*> frontend_required_extensions);
    ~VulkanRasterizer() override;

    void Init(vk::SurfaceKHR surface, const vk::Extent2D& actual_extent) override;
    void DrawFrame() override;
    void OnResized(const vk::Extent2D& actual_extent) override;

private:
    OffscreenImageInfo GetOffscreenImageInfo() const override;
    std::unique_ptr<VulkanDevice> CreateDevice(vk::SurfaceKHR surface,
                                               const vk::Extent2D& actual_extent) const override;

    struct UniformBufferObject;
    UniformBufferObject GetUniformBufferObject() const;
    glm::mat4 GetPushConstant() const;

    void CreateFramebuffers();

    std::unique_ptr<VulkanImmUploadBuffer> vertex_buffer{};
    std::unique_ptr<VulkanImmUploadBuffer> index_buffer{};
    std::unique_ptr<VulkanTexture> texture{};

    vk::raii::RenderPass render_pass = nullptr;
    struct Frame {
        std::unique_ptr<VulkanUniformBufferObject<UniformBufferObject>> uniform{};
        vk::raii::Framebuffer framebuffer = nullptr;
    };
    std::unique_ptr<VulkanFramesInFlight<Frame, 2>> frames;
    std::unique_ptr<VulkanGraphicsPipeline> pipeline;
};

} // namespace Renderer
