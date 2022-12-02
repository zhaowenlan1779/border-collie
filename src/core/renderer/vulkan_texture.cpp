// Copyright 2022 Pengfei Zhu
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <cmath>
#include <stb_image.h>
#include "common/file_util.h"
#include "common/scope_exit.h"
#include "core/renderer/vulkan_allocator.h"
#include "core/renderer/vulkan_buffer.h"
#include "core/renderer/vulkan_texture.h"

namespace Renderer {

VulkanImage::VulkanImage(const VulkanAllocator& allocator_,
                         const vk::ImageCreateInfo& image_create_info,
                         const VmaAllocationCreateInfo& alloc_create_info)
    : allocator(*allocator_) {

    const VkImageCreateInfo& image_create_info_raw = image_create_info;
    const auto result = vmaCreateImage(allocator, &image_create_info_raw, &alloc_create_info,
                                       &image, &allocation, &allocation_info);
    if (result != VK_SUCCESS) {
        vk::throwResultException(vk::Result{result}, "vmaCreateImage");
    }
}

VkImage VulkanImage::operator*() const {
    return image;
}

VulkanImage::~VulkanImage() {
    vmaDestroyImage(allocator, image, allocation);
}

VulkanTexture::VulkanTexture(VulkanAllocator& allocator, const vk::raii::CommandPool& command_pool,
                             const vk::raii::Queue& queue, const std::u8string_view& path) {

    // Load image file
    const auto& contents = Common::ReadFileContents(path);

    int channels_in_file;
    stbi_uc* pixels = stbi_load_from_memory(contents.data(), contents.size(), &width, &height,
                                            &channels_in_file, STBI_rgb_alpha);
    if (!pixels) {
        throw std::runtime_error("Failed to load image");
    }
    SCOPE_EXIT({ stbi_image_free(pixels); });

    const std::size_t pixels_size = width * height * 4;
    const u32 mip_levels = static_cast<u32>(std::floor(std::log2(std::max(width, height)))) + 1;

    // Create image & image_view
    image = std::make_unique<VulkanImage>(allocator,
                                          vk::ImageCreateInfo{
                                              .imageType = vk::ImageType::e2D,
                                              .format = vk::Format::eR8G8B8A8Srgb,
                                              .extent =
                                                  {
                                                      .width = static_cast<u32>(width),
                                                      .height = static_cast<u32>(height),
                                                      .depth = 1,
                                                  },
                                              .mipLevels = mip_levels,
                                              .arrayLayers = 1,
                                              .usage = vk::ImageUsageFlagBits::eTransferSrc |
                                                       vk::ImageUsageFlagBits::eTransferDst |
                                                       vk::ImageUsageFlagBits::eSampled,
                                              .sharingMode = vk::SharingMode::eExclusive,
                                              .initialLayout = vk::ImageLayout::eUndefined,
                                          },
                                          VmaAllocationCreateInfo{
                                              .usage = VMA_MEMORY_USAGE_AUTO,
                                          });

    const auto& handle = allocator.CreateStagingBuffer(command_pool, queue, pixels_size);
    const auto& buffer = *handle;
    std::memcpy(buffer.allocation_info.pMappedData, pixels, pixels_size);

    image_view = vk::raii::ImageView{allocator.device,
                                     vk::ImageViewCreateInfo{
                                         .image = **image,
                                         .viewType = vk::ImageViewType::e2D,
                                         .format = vk::Format::eR8G8B8A8Srgb,
                                         .subresourceRange =
                                             {
                                                 .aspectMask = vk::ImageAspectFlagBits::eColor,
                                                 .baseMipLevel = 0,
                                                 .levelCount = mip_levels,
                                                 .baseArrayLayer = 0,
                                                 .layerCount = 1,
                                             },
                                     }};

    // Upload & create mipmaps
    vmaFlushAllocation(*allocator, buffer.allocation, 0, VK_WHOLE_SIZE);

    const auto LayoutTransition = [this, &buffer](vk::ImageMemoryBarrier2 params) {
        // Fill out remainder of params
        params.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        params.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        params.image = **image;
        params.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
        params.subresourceRange.baseArrayLayer = 0;
        params.subresourceRange.layerCount = 1;
        buffer.command_buffer.pipelineBarrier2({
            .imageMemoryBarrierCount = 1,
            .pImageMemoryBarriers = &params,
        });
    };

    LayoutTransition({
        .srcStageMask = vk::PipelineStageFlagBits2::eTopOfPipe,
        .srcAccessMask = vk::AccessFlags2{},
        .dstStageMask = vk::PipelineStageFlagBits2::eCopy,
        .dstAccessMask = vk::AccessFlagBits2::eTransferWrite,
        .oldLayout = vk::ImageLayout::eUndefined,
        .newLayout = vk::ImageLayout::eTransferDstOptimal,
        .subresourceRange =
            {
                .baseMipLevel = 0,
                .levelCount = mip_levels,
            },
    });
    buffer.command_buffer.copyBufferToImage(
        *buffer, **image, vk::ImageLayout::eTransferDstOptimal,
        {{
            .imageSubresource =
                {
                    .aspectMask = vk::ImageAspectFlagBits::eColor,
                    .mipLevel = 0,
                    .baseArrayLayer = 0,
                    .layerCount = 1,
                },
            .imageExtent =
                {
                    .width = static_cast<u32>(width),
                    .height = static_cast<u32>(height),
                    .depth = 1,
                },
        }});
    // Perform blit
    u32 mip_width = width, mip_height = height;
    for (u32 i = 0; i < mip_levels - 1; ++i) {
        LayoutTransition({
            .srcStageMask = vk::PipelineStageFlagBits2::eTransfer,
            .srcAccessMask = vk::AccessFlagBits2::eTransferWrite,
            .dstStageMask = vk::PipelineStageFlagBits2::eBlit,
            .dstAccessMask = vk::AccessFlagBits2::eTransferRead,
            .oldLayout = vk::ImageLayout::eTransferDstOptimal,
            .newLayout = vk::ImageLayout::eTransferSrcOptimal,
            .subresourceRange =
                {
                    .baseMipLevel = i,
                    .levelCount = 1,
                },
        });
        buffer.command_buffer.blitImage(
            **image, vk::ImageLayout::eTransferSrcOptimal, **image,
            vk::ImageLayout::eTransferDstOptimal,
            {{
                .srcSubresource =
                    {
                        .aspectMask = vk::ImageAspectFlagBits::eColor,
                        .mipLevel = i,
                        .baseArrayLayer = 0,
                        .layerCount = 1,
                    },
                .srcOffsets = {{
                    vk::Offset3D{0, 0, 0},
                    vk::Offset3D{static_cast<s32>(mip_width), static_cast<s32>(mip_height), 1},
                }},
                .dstSubresource =
                    {
                        .aspectMask = vk::ImageAspectFlagBits::eColor,
                        .mipLevel = i + 1,
                        .baseArrayLayer = 0,
                        .layerCount = 1,
                    },
                .dstOffsets = {{
                    vk::Offset3D{0, 0, 0},
                    vk::Offset3D{static_cast<s32>(mip_width / 2), static_cast<s32>(mip_height / 2),
                                 1},
                }},
            }},
            vk::Filter::eLinear);

        // In case the image is not square keep at 1
        if (mip_width > 1)
            mip_width /= 2;
        if (mip_height > 1)
            mip_height /= 2;
    }
    LayoutTransition({
        .srcStageMask = vk::PipelineStageFlagBits2::eTransfer,
        .srcAccessMask = vk::AccessFlagBits2::eTransferWrite,
        .dstStageMask = vk::PipelineStageFlagBits2::eFragmentShader,
        .dstAccessMask = vk::AccessFlagBits2::eShaderRead,
        .oldLayout = vk::ImageLayout::eTransferSrcOptimal,
        .newLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
        .subresourceRange =
            {
                .baseMipLevel = 0,
                .levelCount = mip_levels - 1,
            },
    });
    LayoutTransition({
        .srcStageMask = vk::PipelineStageFlagBits2::eTransfer,
        .srcAccessMask = vk::AccessFlagBits2::eTransferWrite,
        .dstStageMask = vk::PipelineStageFlagBits2::eFragmentShader,
        .dstAccessMask = vk::AccessFlagBits2::eShaderRead,
        .oldLayout = vk::ImageLayout::eTransferDstOptimal,
        .newLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
        .subresourceRange =
            {
                .baseMipLevel = mip_levels - 1,
                .levelCount = 1,
            },
    });
}

VulkanTexture::~VulkanTexture() = default;

} // namespace Renderer
