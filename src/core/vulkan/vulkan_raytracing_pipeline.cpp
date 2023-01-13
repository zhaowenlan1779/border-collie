// Copyright 2022 Pengfei Zhu
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <cstring>
#include <vector>
#include "common/alignment.h"
#include "core/vulkan/vulkan_buffer.h"
#include "core/vulkan/vulkan_device.h"
#include "core/vulkan/vulkan_raytracing_pipeline.h"

namespace Renderer {

VulkanRayTracingPipeline::VulkanRayTracingPipeline(
    const VulkanDevice& device, vk::RayTracingPipelineCreateInfoKHR create_info,
    vk::PipelineLayoutCreateInfo pipeline_layout_info) {

    pipeline_layout = vk::raii::PipelineLayout{*device, pipeline_layout_info};

    create_info.layout = *pipeline_layout;
    pipeline = vk::raii::Pipeline{*device, nullptr, device.pipeline_cache, create_info};

    // Count the groups
    u32 count_rgen = 0;
    u32 count_miss = 0;
    u32 count_hit = 0;
    u32 count_call = 0;
    for (std::size_t i = 0; i < create_info.groupCount; ++i) {
        const auto& group_info = create_info.pGroups[i];
        if (group_info.type == vk::RayTracingShaderGroupTypeKHR::eGeneral) {
            const auto stage = create_info.pStages[group_info.generalShader].stage;
            if (stage == vk::ShaderStageFlagBits::eRaygenKHR) {
                count_rgen++;
            } else if (stage == vk::ShaderStageFlagBits::eMissKHR) {
                count_miss++;
            } else if (stage == vk::ShaderStageFlagBits::eCallableKHR) {
                count_call++;
            }
        } else if (group_info.type == vk::RayTracingShaderGroupTypeKHR::eTrianglesHitGroup ||
                   group_info.type == vk::RayTracingShaderGroupTypeKHR::eProceduralHitGroup) {
            count_hit++;
        }
    }

    const auto& properties2 =
        device.physical_device.getProperties2<vk::PhysicalDeviceProperties2,
                                              vk::PhysicalDeviceRayTracingPipelinePropertiesKHR>();
    const auto& properties = properties2.get<vk::PhysicalDeviceRayTracingPipelinePropertiesKHR>();

    const vk::DeviceSize handle_size_aligned =
        Common::AlignUp(properties.shaderGroupHandleSize, properties.shaderGroupHandleAlignment);

    // Note: We can only use one RayGen at a time, but upload everything now
    rgen_region = vk::StridedDeviceAddressRegionKHR{
        .stride = Common::AlignUp(handle_size_aligned, properties.shaderGroupBaseAlignment),
        .size =
            count_rgen * Common::AlignUp(handle_size_aligned, properties.shaderGroupBaseAlignment),
    };
    miss_region = vk::StridedDeviceAddressRegionKHR{
        .stride = handle_size_aligned,
        .size =
            Common::AlignUp(count_miss * handle_size_aligned, properties.shaderGroupBaseAlignment),
    };
    hit_region = vk::StridedDeviceAddressRegionKHR{
        .stride = handle_size_aligned,
        .size =
            Common::AlignUp(count_hit * handle_size_aligned, properties.shaderGroupBaseAlignment),
    };
    call_region = vk::StridedDeviceAddressRegionKHR{
        .stride = handle_size_aligned,
        .size =
            Common::AlignUp(count_call * handle_size_aligned, properties.shaderGroupBaseAlignment),
    };

    // Allocate buffer
    sbt_buffer = std::make_unique<VulkanBuffer>(
        *device.allocator,
        vk::BufferCreateInfo{
            .size = rgen_region.size + miss_region.size + hit_region.size + call_region.size,
            .usage = vk::BufferUsageFlagBits::eShaderBindingTableKHR |
                     vk::BufferUsageFlagBits::eShaderDeviceAddress,
        },
        VmaAllocationCreateInfo{
            .flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                     VMA_ALLOCATION_CREATE_MAPPED_BIT,
            .usage = VMA_MEMORY_USAGE_AUTO,
        });
    const auto address = device->getBufferAddress({
        .buffer = **sbt_buffer,
    });
    rgen_region.deviceAddress = address;
    miss_region.deviceAddress = rgen_region.deviceAddress + rgen_region.size;
    hit_region.deviceAddress = miss_region.deviceAddress + miss_region.size;
    call_region.deviceAddress = hit_region.deviceAddress + hit_region.size;

    // Write handles
    const auto& handles = pipeline.getRayTracingShaderGroupHandlesKHR<u8>(
        0, create_info.groupCount, properties.shaderGroupHandleSize * create_info.groupCount);

    std::vector<u8> sbt_temp(sbt_buffer->size);
    const auto WriteHandle = [this, &properties = properties, &address, &handles,
                              &sbt_temp](const vk::StridedDeviceAddressRegionKHR& region,
                                         std::size_t idx, u32& count_written) {
        u8* const dst =
            sbt_temp.data() + region.deviceAddress - address + count_written * region.stride;
        std::memcpy(dst, handles.data() + idx * properties.shaderGroupHandleSize,
                    properties.shaderGroupHandleSize);
        count_written++;
    };

    u32 written_rgen = 0;
    u32 written_miss = 0;
    u32 written_hit = 0;
    u32 written_call = 0;
    for (std::size_t i = 0; i < create_info.groupCount; ++i) {
        const auto& group_info = create_info.pGroups[i];
        if (group_info.type == vk::RayTracingShaderGroupTypeKHR::eGeneral) {
            const auto stage = create_info.pStages[group_info.generalShader].stage;
            if (stage == vk::ShaderStageFlagBits::eRaygenKHR) {
                WriteHandle(rgen_region, i, written_rgen);
            } else if (stage == vk::ShaderStageFlagBits::eMissKHR) {
                WriteHandle(miss_region, i, written_miss);
            } else if (stage == vk::ShaderStageFlagBits::eCallableKHR) {
                WriteHandle(call_region, i, written_call);
            }
        } else if (group_info.type == vk::RayTracingShaderGroupTypeKHR::eTrianglesHitGroup ||
                   group_info.type == vk::RayTracingShaderGroupTypeKHR::eProceduralHitGroup) {
            WriteHandle(hit_region, i, written_hit);
        }
    }

    std::memcpy(sbt_buffer->allocation_info.pMappedData, sbt_temp.data(), sbt_temp.size());
    vmaFlushAllocation(sbt_buffer->allocator, sbt_buffer->allocation, 0, VK_WHOLE_SIZE);
}

VulkanRayTracingPipeline::~VulkanRayTracingPipeline() = default;

void VulkanRayTracingPipeline::TraceRays(const vk::raii::CommandBuffer& cmd, u32 width, u32 height,
                                         u32 depth) const {
    cmd.traceRaysKHR(rgen_region, miss_region, hit_region, call_region, width, height, depth);
}

} // namespace Renderer
