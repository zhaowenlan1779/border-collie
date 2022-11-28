// Copyright 2022 Pengfei Zhu
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "common/temp_ptr.h"
#include "core/renderer/vulkan_allocator.h"
#include "core/renderer/vulkan_buffer.h"
#include "core/renderer/vulkan_helpers.hpp"

namespace Renderer {

VulkanBuffer::VulkanBuffer(VulkanAllocator& allocator_,
                           const vk::BufferCreateInfo& buffer_create_info,
                           const VmaAllocationCreateInfo& alloc_create_info)
    : allocator(*allocator_) {

    const VkBufferCreateInfo& buffer_create_info_raw = buffer_create_info;
    const auto result = vmaCreateBuffer(allocator, &buffer_create_info_raw, &alloc_create_info,
                                        &buffer, &allocation, &allocation_info);
    if (result != VK_SUCCESS) {
        vk::throwResultException(vk::Result{result}, "vmaCreateBuffer");
    }
}

VulkanBuffer::~VulkanBuffer() {
    vmaDestroyBuffer(allocator, buffer, allocation);
}

VulkanStagedBuffer::VulkanStagedBuffer(VulkanAllocator& allocator, const u8* data, std::size_t size,
                                       vk::BufferUsageFlags usage)
    : dst_buffer{allocator,
                 {
                     .size = size,
                     .usage = usage | vk::BufferUsageFlagBits::eTransferDst,
                 },
                 {
                     .usage = VMA_MEMORY_USAGE_AUTO,
                 }},
      src_buffer{allocator,
                 {
                     .size = size,
                     .usage = vk::BufferUsageFlagBits::eTransferSrc,
                 },
                 {
                     .flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                              VMA_ALLOCATION_CREATE_MAPPED_BIT,
                     .usage = VMA_MEMORY_USAGE_AUTO,
                 }} {

    std::memcpy(src_buffer.allocation_info.pMappedData, data, size);
    vmaFlushAllocation(allocator.allocator, src_buffer.allocation, 0, VK_WHOLE_SIZE);
}

VulkanStagedBuffer::~VulkanStagedBuffer() = default;

void VulkanStagedBuffer::Upload(const CommandBufferContext& context) {
    context.command_buffer.copyBuffer(src_buffer.buffer, dst_buffer.buffer,
                                      {{
                                          .size = dst_buffer.allocation_info.size,
                                      }});
}

VkBuffer VulkanStagedBuffer::operator*() const {
    return dst_buffer.buffer;
}

VulkanUniformBuffer::VulkanUniformBuffer(VulkanAllocator& allocator_, std::size_t size)
    : allocator{allocator_},
      dst_buffer{allocator,
                 {
                     .size = size,
                     .usage = vk::BufferUsageFlagBits::eUniformBuffer |
                              vk::BufferUsageFlagBits::eTransferDst,
                 },
                 {
                     .flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                              VMA_ALLOCATION_CREATE_HOST_ACCESS_ALLOW_TRANSFER_INSTEAD_BIT |
                              VMA_ALLOCATION_CREATE_MAPPED_BIT,
                     .usage = VMA_MEMORY_USAGE_AUTO,
                 }} {

    VkMemoryPropertyFlags mem_props;
    vmaGetAllocationMemoryProperties(*allocator, dst_buffer.allocation, &mem_props);

    if (!(mem_props & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)) {
        src_buffer = std::make_unique<VulkanBuffer>(
            allocator,
            vk::BufferCreateInfo{
                .size = size,
                .usage = vk::BufferUsageFlagBits::eTransferSrc,
            },
            VmaAllocationCreateInfo{
                .flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                         VMA_ALLOCATION_CREATE_MAPPED_BIT,
                .usage = VMA_MEMORY_USAGE_AUTO,
            });
    }
}

VulkanUniformBuffer::~VulkanUniformBuffer() = default;

void* VulkanUniformBuffer::operator*() const {
    if (src_buffer) {
        return src_buffer->allocation_info.pMappedData;
    } else {
        return dst_buffer.allocation_info.pMappedData;
    }
}

void VulkanUniformBuffer::Upload(const CommandBufferContext& context,
                                 vk::PipelineStageFlags2 dst_stage_mask) {

    const auto& allocation = src_buffer ? src_buffer->allocation : dst_buffer.allocation;
    vmaFlushAllocation(allocator.allocator, allocation, 0, VK_WHOLE_SIZE);

    if (!src_buffer) {
        return;
    }

    context.command_buffer.copyBuffer(src_buffer->buffer, dst_buffer.buffer,
                                      {{
                                          .size = dst_buffer.allocation_info.size,
                                      }});
    context.command_buffer.pipelineBarrier2({
        .bufferMemoryBarrierCount = 1,
        .pBufferMemoryBarriers = TempArr<vk::BufferMemoryBarrier2>{{
            .srcStageMask = vk::PipelineStageFlagBits2::eTransfer,
            .srcAccessMask = vk::AccessFlagBits2::eTransferWrite,
            .dstStageMask = dst_stage_mask,
            .dstAccessMask = vk::AccessFlagBits2::eUniformRead,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .buffer = dst_buffer.buffer,
            .offset = 0,
            .size = dst_buffer.allocation_info.size,
        }},
    });
}

} // namespace Renderer
