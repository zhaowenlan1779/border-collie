// Copyright 2022 Pengfei Zhu
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <vulkan/vulkan.hpp>
#include "common/common_types.h"
#include "core/renderer/vulkan_allocator.h"

/**
 * RAII wrapper for VMA buffer allocations.
 */
class VulkanBuffer {
public:
    explicit VulkanBuffer(VulkanAllocator& allocator,
                          const vk::BufferCreateInfo& buffer_create_info,
                          const VmaAllocationCreateInfo& alloc_create_info);
    ~VulkanBuffer();

    VmaAllocator allocator{};
    VkBuffer buffer{};
    VmaAllocation allocation{};
    VmaAllocationInfo allocation_info{};
};
