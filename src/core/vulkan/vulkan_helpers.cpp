// Copyright 2022 Pengfei Zhu
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <array>
#include "core/vulkan/vulkan_allocator.h"
#include "core/vulkan/vulkan_buffer.h"
#include "core/vulkan/vulkan_helpers.hpp"
#include "core/vulkan/vulkan_texture.h"

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

void ReadAndUploadBuffer(const VulkanDevice& device, const VulkanBuffer& dst_buffer,
                         vk::PipelineStageFlags2 dst_stage_mask, vk::AccessFlags2 dst_access_mask,
                         std::function<void(void*, std::size_t)> read_func) {
    // TODO: Test this more
    static constexpr std::size_t UploadBufferSize = 32 * 1024 * 1024;

    std::size_t bytes_remaining = dst_buffer.size;
    while (bytes_remaining > 0) {
        auto handle = device.allocator->CreateStagingBuffer(UploadBufferSize);
        const auto& src_buffer = *handle;

        const std::size_t to_write = std::min(bytes_remaining, UploadBufferSize);
        read_func(src_buffer.allocation_info.pMappedData, to_write);
        vmaFlushAllocation(**device.allocator, src_buffer.allocation, 0, VK_WHOLE_SIZE);

        const auto& cmd = src_buffer.command_buffer;
        cmd.copyBuffer(*src_buffer, *dst_buffer,
                       {{
                           .dstOffset = dst_buffer.size - bytes_remaining,
                           .size = to_write,
                       }});
        if (bytes_remaining == to_write) { // Last write
            cmd.pipelineBarrier2({
                .bufferMemoryBarrierCount = 1,
                .pBufferMemoryBarriers = TempArr<vk::BufferMemoryBarrier2>{{
                    .srcStageMask = vk::PipelineStageFlagBits2::eTransfer,
                    .srcAccessMask = vk::AccessFlagBits2::eTransferWrite,
                    .dstStageMask = dst_stage_mask,
                    .dstAccessMask = dst_access_mask,
                    .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                    .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                    .buffer = *dst_buffer,
                    .offset = 0,
                    .size = dst_buffer.size,
                }},
            });
        }

        handle.Submit();
        bytes_remaining -= to_write;
    }
}

} // namespace Renderer::Helpers
