// Copyright 2022 Pengfei Zhu
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <memory>
#include <utility>
#include <vk_mem_alloc.h>
#include <vulkan/vulkan_raii.hpp>
#include "common/common_types.h"

namespace Renderer {

class VulkanStagingBuffer;

/**
 * RAII wrapper around VmaAllocator. Also handles the creation and deletion of staging buffers.
 */
class VulkanAllocator final : NonCopyable {
public:
    explicit VulkanAllocator(const vk::raii::Instance& instance,
                             const vk::raii::PhysicalDevice& physical_device,
                             const vk::raii::Device& device);
    ~VulkanAllocator();

    // Creates a staging buffer that returns itself to the ownership of the allocator
    // when submitted
    struct StagingBufferHandle : NonCopyable {
        explicit StagingBufferHandle(VulkanAllocator& allocator,
                                     const vk::raii::CommandPool& command_pool, std::size_t size);
        void Submit(const vk::raii::Queue& queue);
        ~StagingBufferHandle();

        VulkanStagingBuffer& operator*() {
            return *buffer;
        }

        const VulkanStagingBuffer& operator*() const {
            return *buffer;
        }

    private:
        VulkanAllocator& allocator;
        std::unique_ptr<VulkanStagingBuffer> buffer;
        vk::raii::Fence fence;
    };
    [[nodiscard]] StagingBufferHandle CreateStagingBuffer(const vk::raii::CommandPool& command_pool,
                                                          std::size_t size);

    // Should be called from the render thread.
    void CleanupStagingBuffers();

    VmaAllocator operator*() const;

    const vk::raii::Device& device;

private:
    VmaAllocator allocator = nullptr;

    std::vector<std::pair<std::unique_ptr<VulkanStagingBuffer>, vk::raii::Fence>> staging_buffers;
};

} // namespace Renderer
