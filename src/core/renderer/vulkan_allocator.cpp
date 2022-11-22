// Copyright 2022 Pengfei Zhu
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "common/temp_ptr.h"
#include "core/renderer/vulkan_allocator.h"

VulkanAllocator::VulkanAllocator(vk::Instance instance, vk::PhysicalDevice physical_device,
                                 vk::Device device) {
    const auto result = vmaCreateAllocator(TempPtr{VmaAllocatorCreateInfo{
                                               .physicalDevice = physical_device,
                                               .device = device,
                                               .pVulkanFunctions = TempPtr{VmaVulkanFunctions{
                                                   .vkGetInstanceProcAddr = &vkGetInstanceProcAddr,
                                                   .vkGetDeviceProcAddr = &vkGetDeviceProcAddr,
                                               }},
                                               .instance = instance,
                                               .vulkanApiVersion = VK_API_VERSION_1_3,
                                           }},
                                           &allocator);
    if (result != VK_SUCCESS) {
        vk::throwResultException(vk::Result{result}, "vmaCreateAllocator");
    }
}

VmaAllocator VulkanAllocator::operator*() const {
    return allocator;
}

VulkanAllocator::~VulkanAllocator() {
    vmaDestroyAllocator(allocator);
}
