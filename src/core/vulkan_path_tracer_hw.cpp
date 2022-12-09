// Copyright 2022 Pengfei Zhu
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <array>
#include "core/vulkan/vulkan_context.h"
#include "core/vulkan/vulkan_helpers.hpp"
#include "core/vulkan_path_tracer_hw.h"

namespace Renderer {

VulkanPathTracerHW::VulkanPathTracerHW(bool enable_validation_layers,
                                       std::vector<const char*> frontend_required_extensions)
    : VulkanRenderer(enable_validation_layers, std::move(frontend_required_extensions)) {}

VulkanPathTracerHW::~VulkanPathTracerHW() {
    (*device)->waitIdle();
}

std::unique_ptr<VulkanDevice> VulkanPathTracerHW::CreateDevice(
    vk::SurfaceKHR surface, [[maybe_unused]] const vk::Extent2D& actual_extent) const {
    return std::make_unique<VulkanDevice>(
        context->instance, surface,
        std::array{
            VK_KHR_SWAPCHAIN_EXTENSION_NAME,
            VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME,
            VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME,
            VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME,
        },
        Helpers::GenericStructureChain{vk::PhysicalDeviceFeatures2{
                                           .features =
                                               {
                                                   .samplerAnisotropy = VK_TRUE,
                                               },
                                       },
                                       vk::PhysicalDeviceVulkan13Features{
                                           .pipelineCreationCacheControl = VK_TRUE,
                                           .synchronization2 = VK_TRUE,
                                       },
                                       vk::PhysicalDeviceAccelerationStructureFeaturesKHR{
                                           .accelerationStructure = VK_TRUE,
                                       },
                                       vk::PhysicalDeviceRayTracingPipelineFeaturesKHR{
                                           .rayTracingPipeline = VK_TRUE,
                                       }});
}

} // namespace Renderer
