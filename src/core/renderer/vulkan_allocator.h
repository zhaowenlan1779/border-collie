// Copyright 2022 Pengfei Zhu
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <vk_mem_alloc.h>
#include <vulkan/vulkan.hpp>

/**
 * RAII wrapper around VmaAllocator
 */
class VulkanAllocator {
public:
    explicit VulkanAllocator(vk::Instance instance, vk::PhysicalDevice physical_device,
                             vk::Device device);
    ~VulkanAllocator();

    VmaAllocator operator*() const;

private:
    VmaAllocator allocator = nullptr;
};
