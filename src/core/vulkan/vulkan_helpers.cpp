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
    static constexpr std::size_t UploadBufferSize = 8 * 1024 * 1024; // 8MB

    struct Payload {
        std::unique_ptr<VulkanStagingBuffer> src_buffer;
        vk::raii::Fence upload_completed_fence = nullptr;
    };
    std::array<Payload, 2> payloads;

    // Create payload objects
    for (auto& payload : payloads) {
        payload.src_buffer =
            std::make_unique<VulkanStagingBuffer>(*device.allocator, UploadBufferSize);
        payload.upload_completed_fence =
            vk::raii::Fence{*device,
                            {
                                .flags = vk::FenceCreateFlagBits::eSignaled,
                            }};
    }

    std::size_t current = 0;
    std::size_t bytes_remaining = dst_buffer.size;
    while (bytes_remaining > 0) {
        auto& payload = payloads[current];
        if (device->waitForFences({*payload.upload_completed_fence}, VK_FALSE,
                                  std::numeric_limits<u64>::max()) != vk::Result::eSuccess) {

            throw std::runtime_error("Failed to wait for fences");
        }
        device->resetFences({*payload.upload_completed_fence});

        const std::size_t to_write = std::min(bytes_remaining, UploadBufferSize);
        read_func(payload.src_buffer->allocation_info.pMappedData, to_write);
        vmaFlushAllocation(**device.allocator, payload.src_buffer->allocation, 0, VK_WHOLE_SIZE);

        const auto& cmd = payload.src_buffer->command_buffer;
        cmd.reset();

        cmd.begin({});
        cmd.copyBuffer(**payload.src_buffer, *dst_buffer,
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
        cmd.end();

        device.graphics_queue.submit({{
                                         .commandBufferCount = 1,
                                         .pCommandBuffers = TempArr<vk::CommandBuffer>{*cmd},
                                     }},
                                     *payload.upload_completed_fence);

        current = (current + 1) % payloads.size();
        bytes_remaining -= to_write;
    }

    if (device->waitForFences(
            {*payloads[0].upload_completed_fence, *payloads[1].upload_completed_fence}, VK_TRUE,
            std::numeric_limits<u64>::max()) != vk::Result::eSuccess) {

        throw std::runtime_error("Failed to wait for fences");
    }
}

} // namespace Renderer::Helpers
