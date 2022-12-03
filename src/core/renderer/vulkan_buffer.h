// Copyright 2022 Pengfei Zhu
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <cstring>
#include <memory>
#include <vk_mem_alloc.h>
#include <vulkan/vulkan.hpp>
#include "common/common_types.h"
#include "core/renderer/vulkan_helpers.hpp"

namespace Renderer {

class VulkanAllocator;

/**
 * RAII wrapper for VMA buffer allocations.
 */
class VulkanBuffer : NonCopyable {
public:
    explicit VulkanBuffer(const VulkanAllocator& allocator,
                          const vk::BufferCreateInfo& buffer_create_info,
                          const VmaAllocationCreateInfo& alloc_create_info);
    ~VulkanBuffer();

    VkBuffer operator*() const;

    VmaAllocator allocator{};
    VmaAllocation allocation{};
    VmaAllocationInfo allocation_info{};

protected:
    VkBuffer buffer{};
};

/**
 * One-use buffer for uploading data to another buffer. It also keeps a command buffer.
 */
class VulkanStagingBuffer : public VulkanBuffer {
public:
    explicit VulkanStagingBuffer(const VulkanAllocator& allocator,
                                 const vk::raii::CommandPool& command_pool, std::size_t size);
    ~VulkanStagingBuffer();

    vk::raii::CommandBuffer command_buffer = nullptr;
};

// Too many params, let's do it the Vulkan style
struct VulkanImmUploadBufferCreateInfo {
    const u8* data;
    std::size_t size;
    vk::BufferUsageFlags usage;
    vk::PipelineStageFlags2 dst_stage_mask;
    vk::AccessFlags2 dst_access_mask;
};

/**
 * Represents a buffer that is initialized with some CPU data via a staging buffer.
 */
class VulkanImmUploadBuffer : public VulkanBuffer {
public:
    explicit VulkanImmUploadBuffer(VulkanAllocator& allocator,
                                   const vk::raii::CommandPool& command_pool,
                                   const vk::raii::Queue& queue,
                                   const VulkanImmUploadBufferCreateInfo& create_info);
    ~VulkanImmUploadBuffer();
};

class VulkanUniformBuffer : NonCopyable {
public:
    explicit VulkanUniformBuffer(const VulkanAllocator& allocator, std::size_t size);
    ~VulkanUniformBuffer();

    void Upload(const vk::raii::CommandBuffer& command_buffer,
                vk::PipelineStageFlags2 dst_stage_mask);
    void* operator*() const;

    const VulkanAllocator& allocator;
    VulkanBuffer dst_buffer;
    std::unique_ptr<VulkanBuffer> src_buffer;
};

template <typename T>
class VulkanUniformBufferObject : public VulkanUniformBuffer {
    static_assert(VerifyLayoutStd140<T>());

public:
    explicit VulkanUniformBufferObject(const VulkanAllocator& allocator,
                                       vk::PipelineStageFlags2 dst_stage_mask_)
        : VulkanUniformBuffer(allocator, sizeof(T)), dst_stage_mask(dst_stage_mask_) {}

    void Upload(const vk::raii::CommandBuffer& command_buffer) {
        VulkanUniformBuffer::Upload(command_buffer, dst_stage_mask);
    }

    void Update(const T& new_value) {
        std::memcpy(**this, &new_value, sizeof(T));
    }

private:
    vk::PipelineStageFlags2 dst_stage_mask;
};

} // namespace Renderer
