// Copyright 2022 Pengfei Zhu
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <cstring>
#include <optional>
#include <vulkan/vulkan.hpp>
#include "common/common_types.h"
#include "core/renderer/vulkan_helpers.hpp"

namespace Renderer {

class CommandBufferContext;
class VulkanAllocator;

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

/**
 * One-use upload buffer, e.g. vertex/index buffer
 */
class VulkanStagedBuffer {
public:
    explicit VulkanStagedBuffer(VulkanAllocator& allocator, const u8* data, std::size_t size,
                                vk::BufferUsageFlags usage);
    ~VulkanStagedBuffer();

    void Upload(const CommandBufferContext& context);
    VkBuffer operator*() const;

    VulkanBuffer dst_buffer;
    VulkanBuffer src_buffer;
};

class VulkanUniformBuffer {
public:
    explicit VulkanUniformBuffer(VulkanAllocator& allocator, std::size_t size);
    ~VulkanUniformBuffer();

    void Upload(const CommandBufferContext& context, vk::PipelineStageFlags2 dst_stage_mask);
    void* operator*() const;

    VulkanAllocator& allocator;
    VulkanBuffer dst_buffer;
    std::optional<VulkanBuffer> src_buffer;
};

template <typename T>
class VulkanUniformBufferObject : public VulkanUniformBuffer {
    static_assert(VerifyLayoutStd140<T>());

public:
    explicit VulkanUniformBufferObject(VulkanAllocator& allocator,
                                       vk::PipelineStageFlags2 dst_stage_mask_)
        : VulkanUniformBuffer(allocator, sizeof(T)), dst_stage_mask(dst_stage_mask_) {}

    void Upload(const CommandBufferContext& context) {
        VulkanUniformBuffer::Upload(context, dst_stage_mask);
    }

    void Update(const T& new_value) {
        std::memcpy(**this, &new_value, sizeof(T));
    }

private:
    vk::PipelineStageFlags2 dst_stage_mask;
};

} // namespace Renderer
