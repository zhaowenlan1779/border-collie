// Copyright 2022 Pengfei Zhu
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <memory>
#include <utility>
#include <vector>
#include <vulkan/vulkan_raii.hpp>
#include "common/common_types.h"

namespace GLTF {
class Container;
}

namespace Renderer {

class VulkanContext;
class VulkanDevice;
class VulkanImage;
class VulkanGraphicsPipeline;
class VulkanSwapchain;
class VulkanDescriptorSets;
template <typename ExtraData, std::size_t NumFramesInFlight>
class VulkanFramesInFlight;

class Scene;

/**
 * Base class for Vulkan based renderers.
 * Contains common code for swapchain management, postprocessing, etc.
 */
class VulkanRenderer : NonCopyable {
public:
    explicit VulkanRenderer(bool enable_validation_layers,
                            std::vector<const char*> frontend_required_extensions);
    virtual ~VulkanRenderer() = 0;

    //// Public interface
    vk::raii::Instance& GetVulkanInstance();
    const vk::raii::Instance& GetVulkanInstance() const;

    virtual void Init(vk::SurfaceKHR surface, const vk::Extent2D& actual_extent);
    virtual void LoadScene(GLTF::Container& gltf) = 0;
    virtual void DrawFrame() = 0;
    virtual void OnResized(const vk::Extent2D& actual_extent);

protected:
    // Interface for derived classes
    struct OffscreenImageInfo {
        vk::Format format;
        vk::ImageUsageFlags usage;
        vk::PipelineStageFlags2 dst_stage_mask;
        vk::AccessFlags2 dst_access_mask;
    };
    virtual OffscreenImageInfo GetOffscreenImageInfo() const = 0;
    virtual std::unique_ptr<VulkanDevice> CreateDevice(vk::SurfaceKHR surface,
                                                       const vk::Extent2D& actual_extent) const = 0;
    void CreateRenderTargets();
    void PostprocessAndPresent(vk::Semaphore offscreen_render_finished_semaphore);

    std::unique_ptr<VulkanContext> context;
    std::unique_ptr<VulkanDevice> device;
    std::unique_ptr<VulkanSwapchain> swap_chain;
    vk::raii::RenderPass pp_render_pass = nullptr;

    std::unique_ptr<VulkanDescriptorSets> pp_descriptor_sets;
    struct OffscreenFrame {
        vk::raii::Semaphore render_start_semaphore = nullptr;
        std::unique_ptr<VulkanImage> image;
        vk::raii::ImageView image_view = nullptr;
    };
    std::unique_ptr<VulkanFramesInFlight<OffscreenFrame, 2>> pp_frames;
    std::unique_ptr<VulkanGraphicsPipeline> pp_pipeline;

    std::unique_ptr<Scene> scene;
};

} // namespace Renderer
