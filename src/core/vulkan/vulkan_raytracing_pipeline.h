// Copyright 2022 Pengfei Zhu
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <memory>
#include <vulkan/vulkan_raii.hpp>
#include "core/vulkan/vulkan_pipeline.h"

namespace Renderer {

class VulkanBuffer;

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

struct VulkanRayTracingPipelineCreateInfo {
    vk::RayTracingPipelineCreateInfoKHR pipeline_info;
    vk::ArrayProxy<const DescriptorSet> descriptor_sets;
    vk::ArrayProxy<const vk::PushConstantRange> push_constants;
};
class VulkanRayTracingPipeline final : public VulkanPipeline {
public:
    explicit VulkanRayTracingPipeline(const VulkanDevice& device,
                                      VulkanRayTracingPipelineCreateInfo create_info);
    ~VulkanRayTracingPipeline();

    void BeginFrame();

    vk::StridedDeviceAddressRegionKHR rgen_region;
    vk::StridedDeviceAddressRegionKHR miss_region;
    vk::StridedDeviceAddressRegionKHR hit_region;
    vk::StridedDeviceAddressRegionKHR call_region;
    std::unique_ptr<VulkanBuffer> sbt_buffer;
};

} // namespace Renderer
