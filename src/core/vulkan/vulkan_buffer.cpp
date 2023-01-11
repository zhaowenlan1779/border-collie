// Copyright 2022 Pengfei Zhu
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <spdlog/spdlog.h>
#include "common/temp_ptr.h"
#include "core/vulkan/vulkan_allocator.h"
#include "core/vulkan/vulkan_buffer.h"
#include "core/vulkan/vulkan_device.h"
#include "core/vulkan/vulkan_helpers.hpp"

namespace Renderer {

VulkanBuffer::VulkanBuffer(const VulkanAllocator& allocator_,
                           const vk::BufferCreateInfo& buffer_create_info,
                           const VmaAllocationCreateInfo& alloc_create_info)
    : allocator(*allocator_), size(buffer_create_info.size) {

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

VulkanStagingBuffer::VulkanStagingBuffer(const VulkanAllocator& allocator, std::size_t size)
    : VulkanBuffer{allocator,
                   {
                       .size = size,
                       .usage = vk::BufferUsageFlagBits::eTransferSrc,
                   },
                   {
                       .flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                                VMA_ALLOCATION_CREATE_MAPPED_BIT,
                       .usage = VMA_MEMORY_USAGE_AUTO,
                   }} {

    vk::raii::CommandBuffers command_buffers{*allocator.device,
                                             {
                                                 .commandPool = *allocator.device.command_pool,
                                                 .level = vk::CommandBufferLevel::ePrimary,
                                                 .commandBufferCount = 1,
                                             }};
    command_buffer = std::move(command_buffers[0]);
}

VulkanStagingBuffer::~VulkanStagingBuffer() = default;

VulkanImmUploadBuffer::VulkanImmUploadBuffer(VulkanDevice& device,
                                             const VulkanBufferCreateInfo& create_info,
                                             std::function<void(void*, std::size_t)> read_func)
    : VulkanBuffer{*device.allocator,
                   {
                       .size = create_info.size,
                       .usage = create_info.usage | vk::BufferUsageFlagBits::eTransferDst,
                   },
                   {
                       .usage = VMA_MEMORY_USAGE_AUTO,
                   }} {

    Helpers::ReadAndUploadBuffer(device, *this, create_info.dst_stage_mask,
                                 create_info.dst_access_mask, std::move(read_func));
}

VulkanImmUploadBuffer::VulkanImmUploadBuffer(VulkanDevice& device,
                                             const VulkanBufferCreateInfo& create_info,
                                             const u8* data)
    : VulkanBuffer{*device.allocator,
                   {
                       .size = create_info.size,
                       .usage = create_info.usage | vk::BufferUsageFlagBits::eTransferDst,
                   },
                   {
                       .usage = VMA_MEMORY_USAGE_AUTO,
                   }} {

    std::size_t pos = 0;
    Helpers::ReadAndUploadBuffer(device, *this, create_info.dst_stage_mask,
                                 create_info.dst_access_mask,
                                 [data, &pos](void* out, std::size_t size) {
                                     std::memcpy(out, data + pos, size);
                                     pos += size;
                                 });
}

VulkanImmUploadBuffer::~VulkanImmUploadBuffer() = default;

VulkanZeroedBuffer::VulkanZeroedBuffer(VulkanDevice& device,
                                       const VulkanBufferCreateInfo& create_info)
    : VulkanBuffer{*device.allocator,
                   {
                       .size = Common::AlignUp(create_info.size, 4), // Ensure that it can be filled
                       .usage = create_info.usage | vk::BufferUsageFlagBits::eTransferDst,
                   },
                   {
                       .usage = VMA_MEMORY_USAGE_AUTO,
                   }} {

    vk::raii::CommandBuffers command_buffers{*device,
                                             {
                                                 .commandPool = *device.command_pool,
                                                 .level = vk::CommandBufferLevel::ePrimary,
                                                 .commandBufferCount = 1,
                                             }};
    command_buffer = std::move(command_buffers[0]);

    command_buffer.begin({.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit});
    command_buffer.fillBuffer(**this, 0, VK_WHOLE_SIZE, 0);
    command_buffer.pipelineBarrier2({
        .bufferMemoryBarrierCount = 1,
        .pBufferMemoryBarriers = TempArr<vk::BufferMemoryBarrier2>{{
            .srcStageMask = vk::PipelineStageFlagBits2::eTransfer,
            .srcAccessMask = vk::AccessFlagBits2::eTransferWrite,
            .dstStageMask = create_info.dst_stage_mask,
            .dstAccessMask = create_info.dst_access_mask,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .buffer = **this,
            .offset = 0,
            .size = VK_WHOLE_SIZE,
        }},
    });
    command_buffer.end();
    device.graphics_queue.submit({{
        .commandBufferCount = 1,
        .pCommandBuffers = TempArr<vk::CommandBuffer>{*command_buffer},
    }});
}

VulkanZeroedBuffer::~VulkanZeroedBuffer() = default;

VulkanUniformBuffer::VulkanUniformBuffer(const VulkanAllocator& allocator_, std::size_t size)
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

void* VulkanUniformBuffer::operator*() const noexcept {
    if (src_buffer) {
        return src_buffer->allocation_info.pMappedData;
    } else {
        return dst_buffer.allocation_info.pMappedData;
    }
}

void VulkanUniformBuffer::Upload(const vk::raii::CommandBuffer& command_buffer,
                                 vk::PipelineStageFlags2 dst_stage_mask) {

    const auto& allocation = src_buffer ? src_buffer->allocation : dst_buffer.allocation;
    vmaFlushAllocation(*allocator, allocation, 0, VK_WHOLE_SIZE);

    if (!src_buffer) {
        return;
    }

    command_buffer.copyBuffer(**src_buffer, *dst_buffer,
                              {{
                                  .size = dst_buffer.size,
                              }});
    command_buffer.pipelineBarrier2({
        .bufferMemoryBarrierCount = 1,
        .pBufferMemoryBarriers = TempArr<vk::BufferMemoryBarrier2>{{
            .srcStageMask = vk::PipelineStageFlagBits2::eTransfer,
            .srcAccessMask = vk::AccessFlagBits2::eTransferWrite,
            .dstStageMask = dst_stage_mask,
            .dstAccessMask = vk::AccessFlagBits2::eUniformRead,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .buffer = *dst_buffer,
            .offset = 0,
            .size = dst_buffer.size,
        }},
    });
}

} // namespace Renderer
