// Copyright 2022 Pengfei Zhu
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <memory>
#include <vulkan/vulkan_raii.hpp>
#include "common/common_types.h"

namespace Renderer {

class VulkanBuffer;
class VulkanDevice;

constexpr vk::RayTracingShaderGroupCreateInfoKHR General(u32 shader_idx) {
    return {
        .type = vk::RayTracingShaderGroupTypeKHR::eGeneral,
        .generalShader = shader_idx,
        .closestHitShader = VK_SHADER_UNUSED_KHR,
        .anyHitShader = VK_SHADER_UNUSED_KHR,
        .intersectionShader = VK_SHADER_UNUSED_KHR,
    };
}

constexpr vk::RayTracingShaderGroupCreateInfoKHR TrianglesGroup(
    vk::RayTracingShaderGroupCreateInfoKHR in_info) {

    in_info.type = vk::RayTracingShaderGroupTypeKHR::eTrianglesHitGroup;
    in_info.generalShader = VK_SHADER_UNUSED_KHR;
    return in_info;
}

class VulkanRayTracingPipeline : NonCopyable {
public:
    explicit VulkanRayTracingPipeline(const VulkanDevice& device,
                                      vk::RayTracingPipelineCreateInfoKHR create_info,
                                      vk::PipelineLayoutCreateInfo pipeline_layout_info);
    ~VulkanRayTracingPipeline();

    vk::Pipeline operator*() const noexcept {
        return *pipeline;
    }

    void TraceRays(const vk::raii::CommandBuffer& cmd, u32 width, u32 height, u32 depth) const;

    vk::raii::Pipeline pipeline = nullptr;
    vk::raii::PipelineLayout pipeline_layout = nullptr;

    vk::StridedDeviceAddressRegionKHR rgen_region;
    vk::StridedDeviceAddressRegionKHR miss_region;
    vk::StridedDeviceAddressRegionKHR hit_region;
    vk::StridedDeviceAddressRegionKHR call_region;
    std::unique_ptr<VulkanBuffer> sbt_buffer;
};

} // namespace Renderer
