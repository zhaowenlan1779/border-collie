// Copyright 2022 Pengfei Zhu
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <array>
#include <memory>
#include <utility>
#include <vector>
#include <glm/glm.hpp>
#include <vulkan/vulkan_raii.hpp>
#include "core/renderer/vulkan_renderer.h"

namespace Renderer {

class VulkanGraphicsPipeline;
class VulkanImmUploadBuffer;
class VulkanTexture;

class VulkanRasterizer : public VulkanRenderer {
public:
    explicit VulkanRasterizer(bool enable_validation_layers,
                              std::vector<const char*> frontend_required_extensions);
    ~VulkanRasterizer() override;

    void Init(vk::SurfaceKHR surface, const vk::Extent2D& actual_extent) override;
    void OnResized(const vk::Extent2D& actual_extent) override;

private:
    std::unique_ptr<VulkanDevice> CreateDevice(vk::SurfaceKHR surface,
                                               const vk::Extent2D& actual_extent) const override;
    const FrameInFlight& DrawFrameOffscreen() override;

    struct UniformBufferObject;
    UniformBufferObject GetUniformBufferObject() const;
    glm::mat4 GetPushConstant() const;

    void CreateFramebuffers();

    std::unique_ptr<VulkanImmUploadBuffer> vertex_buffer{};
    std::unique_ptr<VulkanImmUploadBuffer> index_buffer{};
    std::unique_ptr<VulkanTexture> texture{};
    std::unique_ptr<VulkanGraphicsPipeline> pipeline;

    std::array<vk::raii::Framebuffer, 2> framebuffers{{nullptr, nullptr}};
};

} // namespace Renderer
