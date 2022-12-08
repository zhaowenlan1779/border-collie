// Copyright 2022 Pengfei Zhu
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <memory>
#include <utility>
#include <vector>
#include <glm/glm.hpp>
#include <vulkan/vulkan_raii.hpp>
#include "common/common_types.h"

namespace Renderer {

class VulkanContext;
class VulkanDevice;
class VulkanImage;
class VulkanImmUploadBuffer;
class VulkanGraphicsPipeline;
class VulkanSwapchain;
class VulkanTexture;
struct FrameInFlight;

/**
 * Base class for Vulkan based renderers.
 * Contains common code for swapchain management, postprocessing, etc.
 */
class VulkanRenderer {
public:
    explicit VulkanRenderer(bool enable_validation_layers,
                            std::vector<const char*> frontend_required_extensions);
    ~VulkanRenderer();

    //// Public interface
    vk::raii::Instance& GetVulkanInstance();
    const vk::raii::Instance& GetVulkanInstance() const;

    virtual void Init(vk::SurfaceKHR surface, const vk::Extent2D& actual_extent);
    void DrawFrame();
    const FrameInFlight& DrawFrameOffscreen();
    void PostprocessAndPresent(const FrameInFlight& offscreen_frame);
    void RecreateSwapchain(const vk::Extent2D& actual_extent);

protected:
    void CreateRenderTargets();
    void CreateRenderTargetFramebuffers();

    struct UniformBufferObject;
    UniformBufferObject GetUniformBufferObject() const;
    glm::mat4 GetPushConstant() const;

    std::unique_ptr<VulkanContext> context;
    std::unique_ptr<VulkanDevice> device;
    std::unique_ptr<VulkanSwapchain> swap_chain;

    struct OffscreenFrame {
        std::unique_ptr<VulkanImage> image;
        vk::raii::ImageView image_view = nullptr;
        vk::raii::Framebuffer framebuffer = nullptr;
    };
    std::array<OffscreenFrame, 2> offscreen_frames;
    std::size_t current_frame = 0;
    std::unique_ptr<VulkanGraphicsPipeline> pipeline;

    std::unique_ptr<VulkanGraphicsPipeline> pp_pipeline;

    std::unique_ptr<VulkanImmUploadBuffer> vertex_buffer{};
    std::unique_ptr<VulkanImmUploadBuffer> index_buffer{};
    std::unique_ptr<VulkanTexture> texture{};
};

} // namespace Renderer
