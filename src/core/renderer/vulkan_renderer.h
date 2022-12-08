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
class VulkanGraphicsPipeline;
class VulkanRendererCore;
class VulkanSwapchain;
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
    void OnResized(const vk::Extent2D& actual_extent);

    std::unique_ptr<VulkanContext> context;
    std::unique_ptr<VulkanDevice> device;
    std::unique_ptr<VulkanSwapchain> swap_chain;
    std::unique_ptr<VulkanRendererCore> core;

    struct OffscreenFrame {
        std::unique_ptr<VulkanImage> image;
        vk::raii::ImageView image_view = nullptr;
    };
    std::array<OffscreenFrame, 2> offscreen_frames;
    std::size_t current_frame = 0;
    std::unique_ptr<VulkanGraphicsPipeline> pp_pipeline;

private:
    void CreateRenderTargets();
    void PostprocessAndPresent(const FrameInFlight& offscreen_frame);
};

} // namespace Renderer
