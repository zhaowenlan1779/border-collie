// Copyright 2022 Pengfei Zhu
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "common/temp_ptr.h"
#include "core/renderer/vulkan_buffer.h"

#include <spdlog/spdlog.h>

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
