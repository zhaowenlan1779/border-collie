// Copyright 2022 Pengfei Zhu
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <algorithm>
#include <limits>
#include <ranges>
#include <spdlog/spdlog.h>
#include "common/ranges.h"
#include "common/temp_ptr.h"
#include "core/vulkan/vulkan_allocator.h"
#include "core/vulkan/vulkan_buffer.h"
#include "core/vulkan/vulkan_device.h"

namespace Renderer {

VulkanAllocator::VulkanAllocator(const vk::raii::Instance& instance, const VulkanDevice& device_)
    : device(device_) {
    const auto result =
        vmaCreateAllocator(TempPtr{VmaAllocatorCreateInfo{
                               .flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT,
                               .physicalDevice = *device.physical_device,
                               .device = **device,
                               .instance = *instance,
                               .vulkanApiVersion = VK_API_VERSION_1_3,
                           }},
                           &allocator);
    if (result != VK_SUCCESS) {
        vk::throwResultException(vk::Result{result}, "vmaCreateAllocator");
    }
}

VulkanAllocator::~VulkanAllocator() {
    if (!staging_buffers.empty()) {
        vk::Result result;
        do {
            result = device->waitForFences(
                Common::VectorFromRange(
                    staging_buffers |
                    std::views::transform([](const auto& pair) { return *pair.second; })),
                VK_TRUE, std::numeric_limits<u64>::max());
        } while (result == vk::Result::eTimeout);
    }
    staging_buffers.clear();

    vmaDestroyAllocator(allocator);
}

VulkanAllocator::StagingBufferHandle::StagingBufferHandle(VulkanAllocator& allocator_,
                                                          std::size_t size)
    : allocator(allocator_),
      buffer(std::make_unique<VulkanStagingBuffer>(allocator, size)), fence{*allocator.device,
                                                                            vk::FenceCreateInfo{}} {

    buffer->command_buffer.begin({.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit});
}

void VulkanAllocator::StagingBufferHandle::Submit() {
    buffer->command_buffer.end();
    allocator.device.graphics_queue.submit(
        {{
            .commandBufferCount = 1,
            .pCommandBuffers = TempArr<vk::CommandBuffer>{*buffer->command_buffer},
        }},
        *fence);

    allocator.staging_buffers.emplace_back(std::move(buffer), std::move(fence));
}

VulkanAllocator::StagingBufferHandle::~StagingBufferHandle() {
    if (buffer) {
        SPDLOG_WARN("Staging buffer not uploaded");
    }
}

VulkanAllocator::StagingBufferHandle VulkanAllocator::CreateStagingBuffer(std::size_t size) {
    CleanupStagingBuffers();
    return StagingBufferHandle{*this, size};
}

void VulkanAllocator::CleanupStagingBuffers() {
    std::erase_if(staging_buffers,
                  [](const auto& pair) { return pair.second.getStatus() == vk::Result::eSuccess; });
}

} // namespace Renderer
