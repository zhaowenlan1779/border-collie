// Copyright 2022 Pengfei Zhu
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <limits>
#include <spdlog/spdlog.h>
#include "common/temp_ptr.h"
#include "core/vulkan/vulkan_device.h"
#include "core/vulkan/vulkan_swapchain.h"

namespace Renderer {

static vk::SurfaceFormatKHR SelectSurfaceFormat(const std::vector<vk::SurfaceFormatKHR>& formats) {
    for (const auto format : formats) {
        if (format.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear &&
            format.format == vk::Format::eB8G8R8A8Srgb) {
            return format;
        }
    }
    return formats[0];
}

static vk::PresentModeKHR SelectPresentMode(const std::vector<vk::PresentModeKHR>& present_modes) {
    for (const auto mode : present_modes) {
        if (mode == vk::PresentModeKHR::eMailbox) {
            return mode;
        }
    }
    return present_modes[0];
}

VulkanSwapchain::VulkanSwapchain(const VulkanDevice& device_, const vk::Extent2D& extent_)
    : device(device_) {
    surface_format =
        SelectSurfaceFormat(device.physical_device.getSurfaceFormatsKHR(*device.surface));
    const auto present_mode =
        SelectPresentMode(device.physical_device.getSurfacePresentModesKHR(*device.surface));

    const auto& capabilities = device.physical_device.getSurfaceCapabilitiesKHR(*device.surface);
    extent = vk::Extent2D{
        std::clamp(extent_.width, capabilities.minImageExtent.width,
                   capabilities.maxImageExtent.width),
        std::clamp(extent_.height, capabilities.minImageExtent.height,
                   capabilities.maxImageExtent.height),
    };
    const u32 image_count = std::min(capabilities.minImageCount + 1, capabilities.maxImageCount);
    swap_chain = vk::raii::SwapchainKHR{
        *device,
        {
            .surface = *device.surface,
            .minImageCount = image_count,
            .imageFormat = surface_format.format,
            .imageColorSpace = surface_format.colorSpace,
            .imageExtent = extent,
            .imageArrayLayers = 1,
            .imageUsage = vk::ImageUsageFlagBits::eColorAttachment,
            .imageSharingMode = device.queue_family_indices.size() > 1
                                    ? vk::SharingMode::eConcurrent
                                    : vk::SharingMode::eExclusive,
            .queueFamilyIndexCount = static_cast<u32>(device.queue_family_indices.size()),
            .pQueueFamilyIndices = device.queue_family_indices.data(),
            .preTransform = capabilities.currentTransform,
            .presentMode = present_mode,
            .oldSwapchain = *swap_chain,
        }};

    image_views.clear();
    for (const auto& image : swap_chain.getImages()) {
        image_views.emplace_back(*device, vk::ImageViewCreateInfo{
                                              .image = image,
                                              .viewType = vk::ImageViewType::e2D,
                                              .format = surface_format.format,
                                              .subresourceRange =
                                                  {
                                                      .aspectMask = vk::ImageAspectFlagBits::eColor,
                                                      .baseMipLevel = 0,
                                                      .levelCount = 1,
                                                      .baseArrayLayer = 0,
                                                      .layerCount = 1,
                                                  },
                                          });
    }
}

VulkanSwapchain::~VulkanSwapchain() = default;

void VulkanSwapchain::CreateFramebuffers(const vk::raii::RenderPass& render_pass) {
    framebuffers.clear();
    for (const auto& image_view : image_views) {
        framebuffers.emplace_back(*device, vk::FramebufferCreateInfo{
                                               .renderPass = *render_pass,
                                               .attachmentCount = 1,
                                               .pAttachments = TempArr<vk::ImageView>{*image_view},
                                               .width = extent.width,
                                               .height = extent.height,
                                               .layers = 1,
                                           });
    }
}

void VulkanSwapchain::Present(const vk::Semaphore& wait_semaphore) {
    const auto present_result = device.present_queue.presentKHR({
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = TempArr<vk::Semaphore>{wait_semaphore},
        .swapchainCount = 1,
        .pSwapchains = TempArr<vk::SwapchainKHR>{*swap_chain},
        .pImageIndices = TempArr<u32>{current_image_index},
    });
    if (present_result != vk::Result::eSuccess) {
        throw std::runtime_error("Failed to present");
    }
}

std::optional<const std::reference_wrapper<vk::raii::Framebuffer>> VulkanSwapchain::AcquireImage(
    const vk::Semaphore& image_available_semaphore) {

    const auto& [result, image_index] =
        swap_chain.acquireNextImage(std::numeric_limits<u64>::max(), image_available_semaphore);
    if (result == vk::Result::eErrorOutOfDateKHR) {
        SPDLOG_WARN("Swapchain is out of date, ignoring frame");
        return {};
    } else if (result != vk::Result::eSuccess) {
        throw std::runtime_error("Failed to acquire next image");
    }

    current_image_index = image_index;
    return framebuffers[image_index];
}

} // namespace Renderer
