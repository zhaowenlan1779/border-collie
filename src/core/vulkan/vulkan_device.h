// Copyright 2022 Pengfei Zhu
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <memory>
#include <string_view>
#include <utility>
#include <vulkan/vulkan_raii.hpp>
#include "common/common_types.h"

namespace Renderer {

class VulkanAllocator;

namespace Helpers {
template <typename T>
class GenericStructureChain;
}

class VulkanDevice : NonCopyable {
public:
    explicit VulkanDevice(
        const vk::raii::Instance& instance, vk::SurfaceKHR surface,
        const vk::ArrayProxy<const char* const>& extensions,
        const Helpers::GenericStructureChain<vk::PhysicalDeviceFeatures2>& features);
    ~VulkanDevice();

    vk::raii::Device& operator*() {
        return device;
    }
    const vk::raii::Device& operator*() const {
        return device;
    }
    vk::raii::Device* operator->() {
        return &device;
    }
    const vk::raii::Device* operator->() const {
        return &device;
    }

    vk::raii::SurfaceKHR surface = nullptr;
    vk::raii::PhysicalDevice physical_device = nullptr;
    vk::raii::Device device = nullptr;

    vk::raii::Queue graphics_queue = nullptr;
    u32 graphics_queue_family = 0;
    vk::raii::Queue present_queue = nullptr;
    u32 present_queue_family = 0;
    std::vector<u32> queue_family_indices;

    vk::raii::CommandPool command_pool = nullptr;
    std::unique_ptr<VulkanAllocator> allocator;

    static constexpr std::u8string_view PipelineCachePath{u8"cache.bin"};
    vk::raii::PipelineCache pipeline_cache = nullptr;

private:
    bool CreateDevice(const vk::raii::Instance& instance, vk::raii::PhysicalDevice& physical_device,
                      const vk::ArrayProxy<const char* const>& extensions,
                      const Helpers::GenericStructureChain<vk::PhysicalDeviceFeatures2>& features);
};

} // namespace Renderer
