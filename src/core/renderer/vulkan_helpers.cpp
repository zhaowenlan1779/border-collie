// Copyright 2022 Pengfei Zhu
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "core/renderer/vulkan_helpers.hpp"
#include "core/renderer/vulkan_texture.h"

namespace Renderer::Helpers {

void ImageLayoutTransition(const vk::raii::CommandBuffer& command_buffer,
                           const std::unique_ptr<VulkanImage>& image,
                           vk::ImageMemoryBarrier2 params) {

    // Fill out remainder of params
    params.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    params.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    params.image = **image;
    params.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
    params.subresourceRange.baseArrayLayer = 0;
    params.subresourceRange.layerCount = 1;
    command_buffer.pipelineBarrier2({
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers = &params,
    });
}

} // namespace Renderer::Helpers
