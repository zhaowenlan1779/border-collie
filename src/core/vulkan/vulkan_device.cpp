// Copyright 2022 Pengfei Zhu
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <filesystem>
#include <fstream>
#include <set>
#include <spdlog/spdlog.h>
#include "common/file_util.h"
#include "common/ranges.h"
#include "core/vulkan/vulkan_allocator.h"
#include "core/vulkan/vulkan_device.h"
#include "core/vulkan/vulkan_helpers.hpp"

namespace Renderer {

VulkanDevice::VulkanDevice(
    const vk::raii::Instance& instance, vk::SurfaceKHR surface_,
    const vk::ArrayProxy<const char* const>& extensions,
    const Helpers::GenericStructureChain<vk::PhysicalDeviceFeatures2>& features)
    : surface{instance, surface_} {

    vk::raii::PhysicalDevices physical_devices{instance};

    // Prefer discrete GPUs
    const auto result = std::ranges::find_if(
        physical_devices, [this, &instance, &extensions, &features](vk::raii::PhysicalDevice& it) {
            return it.getProperties().deviceType == vk::PhysicalDeviceType::eDiscreteGpu &&
                   CreateDevice(instance, it, extensions, features);
        });
    if (result != physical_devices.end()) {
        return;
    }

    // Try everything else
    const auto result_2 = std::ranges::find_if(
        physical_devices, [this, &instance, &extensions, &features](vk::raii::PhysicalDevice& it) {
            return it.getProperties().deviceType != vk::PhysicalDeviceType::eDiscreteGpu &&
                   CreateDevice(instance, it, extensions, features);
        });
    if (result_2 == physical_devices.end()) {
        throw std::runtime_error("Failed to create any device");
    }
}

bool VulkanDevice::CreateDevice(
    const vk::raii::Instance& instance, vk::raii::PhysicalDevice& physical_device_,
    const vk::ArrayProxy<const char* const>& extensions,
    const Helpers::GenericStructureChain<vk::PhysicalDeviceFeatures2>& features) {

    physical_device = std::move(physical_device_);
    const std::string device_name{physical_device.getProperties().deviceName};

    const auto& queue_families = physical_device.getQueueFamilyProperties();
    graphics_queue_family = *std::ranges::find_if(
        std::ranges::iota_view<u32, u32>(0, static_cast<u32>(queue_families.size())),
        [&queue_families](u32 i) {
            // TODO: Support separate queues
            return static_cast<bool>(queue_families[i].queueFlags & vk::QueueFlagBits::eGraphics) &&
                   static_cast<bool>(queue_families[i].queueFlags & vk::QueueFlagBits::eCompute);
        });
    present_queue_family = *std::ranges::find_if(
        std::ranges::iota_view<u32, u32>(0, static_cast<u32>(queue_families.size())),
        [this](u32 i) { return physical_device.getSurfaceSupportKHR(i, *surface); });

    if (graphics_queue_family == queue_families.size() ||
        present_queue_family == queue_families.size()) {
        SPDLOG_WARN("Missing queue families {}", device_name);
        return false;
    }

    const std::set<u32> family_ids{graphics_queue_family, present_queue_family};
    float priority = 1.0f;

    const auto& extensions_raw = Common::VectorFromRange(
        extensions | std::views::transform([](const std::string_view& str) { return str.data(); }));
    try {
        device = vk::raii::Device{
            physical_device,
            {
                .pNext = features,
                .queueCreateInfoCount = static_cast<u32>(family_ids.size()),
                .pQueueCreateInfos =
                    Common::VectorFromRange(family_ids |
                                            std::views::transform([&priority](u32 family_id) {
                                                return vk::DeviceQueueCreateInfo{
                                                    .queueFamilyIndex = family_id,
                                                    .queueCount = 1,
                                                    .pQueuePriorities = &priority,
                                                };
                                            }))
                        .data(),
                .enabledExtensionCount = static_cast<u32>(extensions_raw.size()),
                .ppEnabledExtensionNames = extensions_raw.data(),
            }};
    } catch (...) {
        SPDLOG_WARN("Failed to create logical device {}, possibly missing features", device_name);
        return false;
    }

    queue_family_indices.assign(family_ids.begin(), family_ids.end());
    graphics_queue = device.getQueue(graphics_queue_family, 0);
    present_queue = device.getQueue(present_queue_family, 0);
    SPDLOG_INFO("Selected physical device {}", device_name);

    command_pool =
        vk::raii::CommandPool{device,
                              {
                                  .flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer,
                                  .queueFamilyIndex = graphics_queue_family,
                              }};

    allocator = std::make_unique<VulkanAllocator>(instance, *this);

    const auto& data = Common::ReadFileContents(PipelineCachePath);
    pipeline_cache = vk::raii::PipelineCache{
        device,
        {
            .flags = vk::PipelineCacheCreateFlagBits::eExternallySynchronized,
            .initialDataSize = data.size(),
            .pInitialData = data.data(),
        }};
    return true;
}

VulkanDevice::~VulkanDevice() {
    if (!*device) {
        return;
    }

    device.waitIdle();

    // Save pipeline cache
    const auto& data = pipeline_cache.getData();

    std::ofstream out_file(std::filesystem::path{PipelineCachePath}, std::ios::binary);
    out_file.write(reinterpret_cast<const char*>(data.data()), data.size());
}

} // namespace Renderer
