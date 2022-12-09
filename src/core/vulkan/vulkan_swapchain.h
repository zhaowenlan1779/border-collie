// Copyright 2022 Pengfei Zhu
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <utility>
#include <vulkan/vulkan_raii.hpp>
#include "common/common_types.h"

namespace Renderer {

class VulkanDevice;
class VulkanGraphicsPipeline;

class VulkanSwapchain : NonCopyable {
public:
    explicit VulkanSwapchain(const VulkanDevice& device, const vk::Extent2D& extent);
    ~VulkanSwapchain();

    void CreateFramebuffers(const vk::raii::RenderPass& render_pass);

    const VulkanDevice& device;
    vk::SurfaceFormatKHR surface_format{};
    vk::raii::SwapchainKHR swap_chain = nullptr;
    vk::Extent2D extent{};

    std::vector<vk::raii::ImageView> image_views;
    std::vector<vk::raii::Framebuffer> framebuffers;
    u32 current_image_index = 0;

    std::optional<const std::reference_wrapper<vk::raii::Framebuffer>> AcquireImage(
        const vk::Semaphore& image_available_semaphore);
    void Present(const vk::Semaphore& wait_semaphore);
};

} // namespace Renderer
