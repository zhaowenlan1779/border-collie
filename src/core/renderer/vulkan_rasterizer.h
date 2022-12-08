// Copyright 2022 Pengfei Zhu
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <memory>
#include <utility>
#include <glm/glm.hpp>
#include <vulkan/vulkan_raii.hpp>
#include "core/renderer/vulkan_renderer_core.h"

namespace Renderer {

class VulkanGraphicsPipeline;
class VulkanImmUploadBuffer;
class VulkanRenderer;
class VulkanTexture;

class VulkanRasterizer : public VulkanRendererCore {
public:
    explicit VulkanRasterizer(const VulkanRenderer& renderer);
    ~VulkanRasterizer() override;

    void OnResized(const vk::Extent2D& actual_extent) override;
    const FrameInFlight& DrawFrame() override;

private:
    struct UniformBufferObject;
    UniformBufferObject GetUniformBufferObject() const;
    glm::mat4 GetPushConstant() const;

    void CreateFramebuffers();

    const VulkanRenderer& renderer;
    std::unique_ptr<VulkanImmUploadBuffer> vertex_buffer{};
    std::unique_ptr<VulkanImmUploadBuffer> index_buffer{};
    std::unique_ptr<VulkanTexture> texture{};
    std::unique_ptr<VulkanGraphicsPipeline> pipeline;

    std::array<vk::raii::Framebuffer, 2> framebuffers{{nullptr, nullptr}};
};

} // namespace Renderer
