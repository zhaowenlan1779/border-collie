// Copyright 2022 Pengfei Zhu
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <cmath>
#include <fstream>
#include <citycrc.h>
#include <spdlog/spdlog.h>
#include <stb_image.h>
#include <stb_image_resize.h>
#include <stb_image_write.h>
#include "common/file_util.h"
#include "core/vulkan/vulkan_allocator.h"
#include "core/vulkan/vulkan_buffer.h"
#include "core/vulkan/vulkan_device.h"
#include "core/vulkan/vulkan_texture.h"

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

VulkanImage::~VulkanImage() {
    vmaDestroyImage(allocator, image, allocation);
}

struct StbImage : NonCopyable {
public:
    explicit StbImage(int width_, int height_)
        : width(width_), height(height_), size(width * height * std::size_t{4}) {

        pixels = reinterpret_cast<stbi_uc*>(std::malloc(size));
    }
    explicit StbImage(std::vector<u8> contents) {
        int channels_in_file;
        pixels = stbi_load_from_memory(contents.data(), static_cast<int>(contents.size()), &width,
                                       &height, &channels_in_file, STBI_rgb_alpha);
        if (!pixels) {
            throw std::runtime_error("Failed to load image");
        }
        size = width * height * std::size_t{4};
    }
    ~StbImage() {
        // stbi_image_free() is just free()
        std::free(pixels);
    }

    stbi_uc* pixels{};
    int width{};
    int height{};
    std::size_t size{};
};

static void StbiWriteCallback(void* context, void* data, int size) {
    auto& out_file = *reinterpret_cast<std::ofstream*>(context);
    out_file.write(reinterpret_cast<const char*>(data), size);
}

VulkanTexture::VulkanTexture(VulkanDevice& device, std::vector<u8> file_data, bool mipmaps) {
    // Load image file
    auto image_data = std::make_unique<StbImage>(std::move(file_data));
    width = static_cast<u32>(image_data->width);
    height = static_cast<u32>(image_data->height);

    const u32 mip_levels =
        mipmaps ? static_cast<u32>(std::floor(std::log2(std::max(width, height)))) + 1 : 1;

    // Create image & image_view
    image = std::make_unique<VulkanImage>(
        *device.allocator,
        vk::ImageCreateInfo{
            .imageType = vk::ImageType::e2D,
            .format = vk::Format::eR8G8B8A8Srgb,
            .extent =
                {
                    .width = width,
                    .height = height,
                    .depth = 1,
                },
            .mipLevels = mip_levels,
            .arrayLayers = 1,
            .usage = vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled,
            .sharingMode = vk::SharingMode::eExclusive,
            .initialLayout = vk::ImageLayout::eUndefined,
        },
        VmaAllocationCreateInfo{
            .usage = VMA_MEMORY_USAGE_AUTO,
        });
    image_view =
        vk::raii::ImageView{*device, vk::ImageViewCreateInfo{
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

    // Determine & create mipmaps folder
    const std::filesystem::path mipmaps_folder = device.startup_path / u8"mipmaps";
    std::filesystem::create_directory(mipmaps_folder);

    // Hash the image to mark mipmap version, if necessary
    std::string hash;
    if (mip_levels > 1) {
        const auto& [hash_h, hash_l] =
            CityHashCrc128(reinterpret_cast<const char*>(image_data->pixels), image_data->size);
        hash = fmt::format("{:08x}{:08x}", hash_h, hash_l);
    }

    u32 mip_width = width, mip_height = height;
    for (u32 i = 0; i < mip_levels; ++i) {
        if (i != 0) {
            // Try load mipmap. If not successful, resize it on the fly and save it.
            // Determine mipmap path
            const auto mipmap_name = fmt::format("{}.{}.png", hash, i);
            const auto mipmap_path = mipmaps_folder / std::filesystem::u8path(mipmap_name);

            const auto last_image = std::move(image_data);
            image_data.reset();
            if (std::filesystem::exists(mipmap_path)) {
                try {
                    image_data = std::make_unique<StbImage>(Common::ReadFileContents(mipmap_path));
                } catch (...) {
                    SPDLOG_WARN("Could not load {} mip level {} from file, regenerating.", hash, i);
                    image_data.reset();
                }
                if (static_cast<u32>(image_data->width) != mip_width ||
                    static_cast<u32>(image_data->height) != mip_height) {

                    SPDLOG_WARN("{} mip level {} has incorrect dimensions, regenerating.", hash, i);
                    image_data.reset();
                }
            } else {
                SPDLOG_WARN("{} mip level {} does not exist, generating.", hash, i);
            }

            if (!image_data) { // Resize on the fly and save
                image_data = std::make_unique<StbImage>(mip_width, mip_height);
                if (!stbir_resize_uint8_srgb(last_image->pixels, last_image->width,
                                             last_image->height, 0, image_data->pixels,
                                             image_data->width, image_data->height, 0, 4, 3, 0)) {
                    throw std::runtime_error("Could not resize image");
                }

                std::ofstream out_file{mipmap_path, std::ios::binary};
                if (!stbi_write_png_to_func(&StbiWriteCallback, &out_file, image_data->width,
                                            image_data->height, 4, image_data->pixels, 0)) {
                    SPDLOG_WARN("Failed to write {} mip level {} to file", hash, i);
                }
            }
        }

        // Upload mipmap
        auto handle = device.allocator->CreateStagingBuffer(image_data->size);
        const auto& buffer = *handle;

        std::memcpy(buffer.allocation_info.pMappedData, image_data->pixels, image_data->size);
        vmaFlushAllocation(**device.allocator, buffer.allocation, 0, VK_WHOLE_SIZE);

        Helpers::ImageLayoutTransition(buffer.command_buffer, image,
                                       {
                                           .srcStageMask = vk::PipelineStageFlagBits2::eTopOfPipe,
                                           .srcAccessMask = vk::AccessFlags2{},
                                           .dstStageMask = vk::PipelineStageFlagBits2::eCopy,
                                           .dstAccessMask = vk::AccessFlagBits2::eTransferWrite,
                                           .oldLayout = vk::ImageLayout::eUndefined,
                                           .newLayout = vk::ImageLayout::eTransferDstOptimal,
                                           .subresourceRange =
                                               {
                                                   .baseMipLevel = i,
                                                   .levelCount = 1,
                                               },
                                       });
        buffer.command_buffer.copyBufferToImage(
            *buffer, **image, vk::ImageLayout::eTransferDstOptimal,
            {{
                .imageSubresource =
                    {
                        .aspectMask = vk::ImageAspectFlagBits::eColor,
                        .mipLevel = i,
                        .baseArrayLayer = 0,
                        .layerCount = 1,
                    },
                .imageExtent =
                    {
                        .width = mip_width,
                        .height = mip_height,
                        .depth = 1,
                    },
            }});
        Helpers::ImageLayoutTransition(
            buffer.command_buffer, image,
            {
                .srcStageMask = vk::PipelineStageFlagBits2::eTransfer,
                .srcAccessMask = vk::AccessFlagBits2::eTransferWrite,
                .dstStageMask = vk::PipelineStageFlagBits2::eFragmentShader,
                .dstAccessMask = vk::AccessFlagBits2::eShaderRead,
                .oldLayout = vk::ImageLayout::eTransferDstOptimal,
                .newLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
                .subresourceRange =
                    {
                        .baseMipLevel = i,
                        .levelCount = 1,
                    },
            });

        handle.Submit();

        // In case the image is not square keep at 1
        if (mip_width > 1)
            mip_width /= 2;
        if (mip_height > 1)
            mip_height /= 2;
    }
}

VulkanTexture::~VulkanTexture() = default;

} // namespace Renderer
