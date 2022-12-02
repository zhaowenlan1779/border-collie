// Copyright 2022 Pengfei Zhu
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <algorithm>
#include <limits>
#include <ranges>
#include "common/ranges.h"
#include "common/temp_ptr.h"
#include "core/renderer/vulkan_allocator.h"
#include "core/renderer/vulkan_buffer.h"

namespace Renderer {

VulkanAllocator::VulkanAllocator(const vk::raii::Instance& instance,
                                 const vk::raii::PhysicalDevice& physical_device,
                                 const vk::raii::Device& device_)
    : device(device_) {
    const auto result = vmaCreateAllocator(TempPtr{VmaAllocatorCreateInfo{
                                               .physicalDevice = *physical_device,
                                               .device = *device,
                                               .instance = *instance,
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
    if (!staging_buffers.empty()) {
        vk::Result result;
        do {
            result = device.waitForFences(
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
                                                          const vk::raii::CommandPool& command_pool,
                                                          const vk::raii::Queue& queue,
                                                          std::size_t size)
    : allocator(allocator_),
      buffer(std::make_unique<VulkanStagingBuffer>(allocator, command_pool, queue, size)),
      fence{allocator.device, vk::FenceCreateInfo{}} {

    buffer->command_buffer.begin({.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit});
}

VulkanAllocator::StagingBufferHandle::~StagingBufferHandle() {
    buffer->command_buffer.end();
    buffer->queue.submit({{
                             .commandBufferCount = 1,
                             .pCommandBuffers = TempArr<vk::CommandBuffer>{*buffer->command_buffer},
                         }},
                         *fence);

    allocator.staging_buffers.emplace_back(std::move(buffer), std::move(fence));
}

VulkanAllocator::StagingBufferHandle VulkanAllocator::CreateStagingBuffer(
    const vk::raii::CommandPool& command_pool, const vk::raii::Queue& queue, std::size_t size) {

    CleanupStagingBuffers();
    return StagingBufferHandle{*this, command_pool, queue, size};
}

void VulkanAllocator::CleanupStagingBuffers() {
    std::erase_if(staging_buffers,
                  [](const auto& pair) { return pair.second.getStatus() == vk::Result::eSuccess; });
}

} // namespace Renderer
