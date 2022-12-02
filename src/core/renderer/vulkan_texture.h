// Copyright 2022 Pengfei Zhu
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <filesystem>
#include <memory>
#include <utility>
#include <vk_mem_alloc.h>
#include <vulkan/vulkan_raii.hpp>
#include "common/common_types.h"

namespace Renderer {

class VulkanAllocator;
class VulkanBuffer;

/**
 * RAII wrapper for VMA image allocations.
 */
class VulkanImage : NonCopyable {
public:
    explicit VulkanImage(const VulkanAllocator& allocator,
                         const vk::ImageCreateInfo& image_create_info,
                         const VmaAllocationCreateInfo& alloc_create_info);
    ~VulkanImage();

    VkImage operator*() const;

    VmaAllocator allocator{};
    VmaAllocation allocation{};
    VmaAllocationInfo allocation_info{};

private:
    VkImage image{};
};

class VulkanTexture : NonCopyable {
public:
    explicit VulkanTexture(VulkanAllocator& allocator, const vk::raii::CommandPool& command_pool,
                           const vk::raii::Queue& queue, const std::filesystem::path& path);
    ~VulkanTexture();

    u32 width{};
    u32 height{};
    std::unique_ptr<VulkanImage> image;
    vk::raii::ImageView image_view = nullptr;
};

} // namespace Renderer
