// Copyright 2022 Pengfei Zhu
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "common/ranges.h"
#include "core/vulkan/vulkan_accel_structure.h"
#include "core/vulkan/vulkan_allocator.h"
#include "core/vulkan/vulkan_buffer.h"
#include "core/vulkan/vulkan_device.h"

namespace Renderer {

VulkanAccelStructureMemory::VulkanAccelStructureMemory(
    const VulkanDevice& device, vk::AccelerationStructureCreateInfoKHR create_info) {

    buffer = std::make_unique<VulkanBuffer>(
        *device.allocator,
        vk::BufferCreateInfo{
            .size = create_info.size,
            .usage = vk::BufferUsageFlagBits::eAccelerationStructureStorageKHR |
                     vk::BufferUsageFlagBits::eShaderDeviceAddress,
        },
        VmaAllocationCreateInfo{
            .usage = VMA_MEMORY_USAGE_AUTO,
        });

    create_info.buffer = **buffer;
    create_info.offset = 0;
    as = vk::raii::AccelerationStructureKHR{*device, create_info};
}

VulkanAccelStructureMemory::~VulkanAccelStructureMemory() = default;

VulkanAccelStructure::VulkanAccelStructure(
    VulkanDevice& device_,
    const vk::ArrayProxy<const vk::AccelerationStructureGeometryKHR>& geometries,
    const vk::ArrayProxy<const vk::AccelerationStructureBuildRangeInfoKHR>& build_ranges,
    vk::AccelerationStructureTypeKHR type_)
    : device(device_), type(type_) {

    Init(geometries, build_ranges);
}

constexpr vk::TransformMatrixKHR ToVulkanMatrix(const glm::mat4& mat) {
    return {std::array{
        std::array{mat[0][0], mat[1][0], mat[2][0], mat[3][0]},
        std::array{mat[0][1], mat[1][1], mat[2][1], mat[3][1]},
        std::array{mat[0][2], mat[1][2], mat[2][2], mat[3][2]},
    }};
}

VulkanAccelStructure::VulkanAccelStructure(const vk::ArrayProxy<const BLASInstance>& instances)
    : device((*instances.begin()).blas.device), type(vk::AccelerationStructureTypeKHR::eTopLevel) {

    const auto instance_geometries = Common::VectorFromRange(
        instances | std::views::transform([this](const auto& instance) {
            return vk::AccelerationStructureInstanceKHR{
                .transform = ToVulkanMatrix(instance.transform),
                .instanceCustomIndex = instance.custom_index,
                .mask = 0xFF,
                .instanceShaderBindingTableRecordOffset = 0,
                .flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR,
                .accelerationStructureReference = device->getBufferAddress({
                    .buffer = **instance.blas.compacted_as->buffer,
                }),
            };
        }));

    instances_buffer = std::make_unique<VulkanImmUploadBuffer>(
        device,
        VulkanBufferCreateInfo{
            .size = instance_geometries.size() * sizeof(vk::AccelerationStructureInstanceKHR),
            .usage = vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR |
                     vk::BufferUsageFlagBits::eShaderDeviceAddress,
            .dst_stage_mask = vk::PipelineStageFlagBits2::eAccelerationStructureBuildKHR,
            .dst_access_mask = vk::AccessFlagBits2::eShaderRead,
        },
        reinterpret_cast<const u8*>(instance_geometries.data()));
    Init(
        vk::AccelerationStructureGeometryKHR{
            .geometryType = vk::GeometryTypeKHR::eInstances,
            .geometry =
                {
                    .instances =
                        {
                            .data =
                                {
                                    .deviceAddress = device->getBufferAddress({
                                        .buffer = **instances_buffer,
                                    }),
                                },
                        },
                },
        },
        vk::AccelerationStructureBuildRangeInfoKHR{
            .primitiveCount = instances.size(),
        });
}

void VulkanAccelStructure::Init(
    const vk::ArrayProxy<const vk::AccelerationStructureGeometryKHR>& geometries,
    const vk::ArrayProxy<const vk::AccelerationStructureBuildRangeInfoKHR>& build_ranges) {

    vk::AccelerationStructureBuildGeometryInfoKHR geometry_info{
        .type = type,
        .flags = vk::BuildAccelerationStructureFlagBitsKHR::ePreferFastTrace |
                 vk::BuildAccelerationStructureFlagBitsKHR::eAllowCompaction,
        .mode = vk::BuildAccelerationStructureModeKHR::eBuild,
        .geometryCount = geometries.size(),
        .pGeometries = geometries.data(),
    };

    // Query sizes and create buffers
    const auto size_info = device->getAccelerationStructureBuildSizesKHR(
        vk::AccelerationStructureBuildTypeKHR::eDevice, geometry_info,
        Common::VectorFromRange(build_ranges | std::views::transform([](const auto& build_range) {
                                    return build_range.primitiveCount;
                                })));

    scratch_buffer =
        std::make_unique<VulkanBuffer>(*device.allocator,
                                       vk::BufferCreateInfo{
                                           .size = size_info.buildScratchSize,
                                           .usage = vk::BufferUsageFlagBits::eStorageBuffer |
                                                    vk::BufferUsageFlagBits::eShaderDeviceAddress,
                                       },
                                       VmaAllocationCreateInfo{
                                           .usage = VMA_MEMORY_USAGE_AUTO,
                                       });

    as = std::make_unique<VulkanAccelStructureMemory>(
        device, vk::AccelerationStructureCreateInfoKHR{
                    .size = size_info.accelerationStructureSize,
                    .type = type,
                });

    vk::raii::CommandBuffers cmdbufs{*device,
                                     {
                                         .commandPool = *device.command_pool,
                                         .level = vk::CommandBufferLevel::ePrimary,
                                         .commandBufferCount = 2,
                                     }};
    build_cmdbuf = std::move(cmdbufs[0]);
    compact_cmdbuf = std::move(cmdbufs[1]);

    build_fence = vk::raii::Fence{*device, vk::FenceCreateInfo{}};
    compact_fence = vk::raii::Fence{*device, vk::FenceCreateInfo{}};

    query_pool =
        vk::raii::QueryPool{*device,
                            {
                                .queryType = vk::QueryType::eAccelerationStructureCompactedSizeKHR,
                                .queryCount = 1,
                            }};

    // Fill out remainder fields
    geometry_info.dstAccelerationStructure = **as;
    geometry_info.scratchData.deviceAddress = device->getBufferAddress({
        .buffer = **scratch_buffer,
    });

    build_cmdbuf.begin({.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit});
    build_cmdbuf.buildAccelerationStructuresKHR(geometry_info, build_ranges.data());
    build_cmdbuf.resetQueryPool(*query_pool, 0, 1);
    build_cmdbuf.pipelineBarrier2({
        .pMemoryBarriers = TempArr<vk::MemoryBarrier2>{{
            .srcStageMask = vk::PipelineStageFlagBits2::eAccelerationStructureBuildKHR,
            .srcAccessMask = vk::AccessFlagBits2::eAccelerationStructureWriteKHR,
            .dstStageMask = vk::PipelineStageFlagBits2::eAccelerationStructureBuildKHR,
            .dstAccessMask = vk::AccessFlagBits2::eAccelerationStructureReadKHR,
        }},
    });
    build_cmdbuf.writeAccelerationStructuresPropertiesKHR(
        **as, vk::QueryType::eAccelerationStructureCompactedSizeKHR, *query_pool, 0);

    build_cmdbuf.end();

    device.graphics_queue.submit({{
                                     .commandBufferCount = 1,
                                     .pCommandBuffers = TempArr<vk::CommandBuffer>{*build_cmdbuf},
                                 }},
                                 *build_fence);
}

VulkanAccelStructure::~VulkanAccelStructure() = default;

void VulkanAccelStructure::Compact() {
    if (compacted) {
        return;
    }
    if (build_fence.getStatus() != vk::Result::eSuccess) {
        return;
    }

    build_fence = nullptr;
    build_cmdbuf = nullptr;
    scratch_buffer.reset();
    instances_buffer.reset();

    const auto [result, compacted_size] = query_pool.getResult<VkDeviceSize>(
        0, 1, sizeof(VkDeviceSize), vk::QueryResultFlagBits::e64);
    if (result != vk::Result::eSuccess) {
        vk::throwResultException(result, "vkGetQueryPoolResult");
        return;
    }
    query_pool = nullptr;

    compacted_as =
        std::make_unique<VulkanAccelStructureMemory>(device, vk::AccelerationStructureCreateInfoKHR{
                                                                 .size = compacted_size,
                                                                 .type = type,
                                                             });

    compact_cmdbuf.begin({.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit});
    compact_cmdbuf.copyAccelerationStructureKHR({
        .src = **as,
        .dst = **compacted_as,
        .mode = vk::CopyAccelerationStructureModeKHR::eCompact,
    });
    compact_cmdbuf.end();

    device.graphics_queue.submit({{
                                     .commandBufferCount = 1,
                                     .pCommandBuffers = TempArr<vk::CommandBuffer>{*compact_cmdbuf},
                                 }},
                                 *compact_fence);
    compacted = true;
}

void VulkanAccelStructure::Cleanup() {
    if (!compacted || !as) {
        return;
    }
    if (compact_fence.getStatus() == vk::Result::eSuccess) {
        compact_fence = nullptr;
        compact_cmdbuf = nullptr;
        as.reset();
    }
}

} // namespace Renderer
