// Copyright 2022 Pengfei Zhu
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <array>
#include <chrono>
#include <glm/gtc/matrix_transform.hpp>
#include "core/vulkan/vulkan_accel_structure.h"
#include "core/vulkan/vulkan_buffer.h"
#include "core/vulkan/vulkan_context.h"
#include "core/vulkan/vulkan_device.h"
#include "core/vulkan/vulkan_helpers.hpp"
#include "core/vulkan/vulkan_raytracing_pipeline.h"
#include "core/vulkan/vulkan_shader.h"
#include "core/vulkan/vulkan_swapchain.h"
#include "core/vulkan/vulkan_texture.h"
#include "core/vulkan_path_tracer_hw.h"

namespace Renderer {

VulkanPathTracerHW::VulkanPathTracerHW(bool enable_validation_layers,
                                       std::vector<const char*> frontend_required_extensions)
    : VulkanRenderer(enable_validation_layers, std::move(frontend_required_extensions)) {}

VulkanPathTracerHW::~VulkanPathTracerHW() {
    (*device)->waitIdle();
}

VulkanRenderer::OffscreenImageInfo VulkanPathTracerHW::GetOffscreenImageInfo() const {
    return {
        .format = vk::Format::eR32G32B32A32Sfloat,
        .usage = vk::ImageUsageFlagBits::eStorage,
        .dst_stage_mask = vk::PipelineStageFlagBits2::eRayTracingShaderKHR,
        .dst_access_mask = vk::AccessFlagBits2::eShaderStorageWrite,
    };
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
                                       vk::PhysicalDeviceVulkan12Features{
                                           .bufferDeviceAddress = VK_TRUE,
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

struct Vertex {
    glm::vec3 pos;
    glm::vec3 color;
    glm::vec2 texCoord;
};

struct VulkanPathTracerHW::UniformBufferObject {
    glm::mat4 model;
    glm::mat4 view;
    glm::mat4 proj;
};

void VulkanPathTracerHW::Init(vk::SurfaceKHR surface, const vk::Extent2D& actual_extent) {
    VulkanRenderer::Init(surface, actual_extent);

    // Buffers & Textures
    const std::vector<Vertex> vertices = {{{-0.5f, -0.5f, 0.0f}, {1.0f, 0.0f, 0.0f}, {1.0f, 0.0f}},
                                          {{0.5f, -0.5f, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f}},
                                          {{0.5f, 0.5f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 1.0f}},
                                          {{-0.5f, 0.5f, 0.0f}, {1.0f, 1.0f, 1.0f}, {1.0f, 1.0f}}};
    vertex_buffer = std::make_unique<VulkanImmUploadBuffer>(
        *device, VulkanImmUploadBufferCreateInfo{
                     .data = reinterpret_cast<const u8*>(vertices.data()),
                     .size = vertices.size() * sizeof(Vertex),
                     .usage = vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR |
                              vk::BufferUsageFlagBits::eShaderDeviceAddress,
                     .dst_stage_mask = vk::PipelineStageFlagBits2::eAccelerationStructureBuildKHR,
                     .dst_access_mask = vk::AccessFlagBits2::eShaderRead,
                 });

    const std::vector<u16> indices = {0, 1, 2, 2, 3, 0};
    index_buffer = std::make_unique<VulkanImmUploadBuffer>(
        *device, VulkanImmUploadBufferCreateInfo{
                     .data = reinterpret_cast<const u8*>(indices.data()),
                     .size = indices.size() * sizeof(u16),
                     .usage = vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR |
                              vk::BufferUsageFlagBits::eShaderDeviceAddress,
                     .dst_stage_mask = vk::PipelineStageFlagBits2::eAccelerationStructureBuildKHR,
                     .dst_access_mask = vk::AccessFlagBits2::eShaderRead,
                 });

    texture = std::make_unique<VulkanTexture>(*device, u8"textures/texture.jpg");

    // Build acceleration structures
    // clang-format off
    blas.clear();
    blas.emplace_back(std::make_unique<VulkanAccelStructure>(
        *device,
        vk::AccelerationStructureGeometryKHR{
            .geometryType = vk::GeometryTypeKHR::eTriangles,
            .geometry =
                {
                    .triangles =
                        {
                            .vertexFormat = vk::Format::eR32G32B32Sfloat,
                            .vertexData =
                                {
                                    .deviceAddress = (*device)->getBufferAddress({
                                        .buffer = **vertex_buffer,
                                    }),
                                },
                            .vertexStride = sizeof(Vertex),
                            .maxVertex = static_cast<u32>(vertices.size()),
                            .indexType = vk::IndexType::eUint16,
                            .indexData =
                                {
                                    .deviceAddress = (*device)->getBufferAddress({
                                        .buffer = **index_buffer,
                                    }),
                                },
                        },
                },
            .flags = vk::GeometryFlagBitsKHR::eOpaque,
        },
        vk::AccelerationStructureBuildRangeInfoKHR{
            .primitiveCount = static_cast<u32>(indices.size() / 3),
        }));
    blas[0]->Compact(true);
    blas[0]->Cleanup(true);
    // clang-format on

    tlas = std::make_unique<VulkanAccelStructure>(blas);
    tlas->Compact(true);
    tlas->Cleanup(true);

    // clang-format off
    pipeline = std::make_unique<VulkanRayTracingPipeline>(*device, VulkanRayTracingPipelineCreateInfo{
        .pipeline_info =
            {
                .stageCount = 3,
                .pStages = TempArr<vk::PipelineShaderStageCreateInfo>{{
                    {
                        .stage = vk::ShaderStageFlagBits::eRaygenKHR,
                        .module =
                            *VulkanShader{**device, u8"core/shaders/raytracing_hw/raytrace.rgen"},
                        .pName = "main",
                    },
                    {
                        .stage = vk::ShaderStageFlagBits::eMissKHR,
                        .module =
                            *VulkanShader{**device, u8"core/shaders/raytracing_hw/raytrace.rmiss"},
                        .pName = "main",
                    },
                    {
                        .stage = vk::ShaderStageFlagBits::eClosestHitKHR,
                        .module =
                            *VulkanShader{**device, u8"core/shaders/raytracing_hw/raytrace.rchit"},
                        .pName = "main",
                    },
                }},
                .groupCount = 3,
                .pGroups = TempArr<vk::RayTracingShaderGroupCreateInfoKHR>{{
                    General(0),
                    General(1),
                    TrianglesGroup({
                        .closestHitShader = 2,
                        .anyHitShader = VK_SHADER_UNUSED_KHR,
                        .intersectionShader = VK_SHADER_UNUSED_KHR,
                    }),
                }},
            },
        .descriptor_sets = {{
            {
                .type = vk::DescriptorType::eAccelerationStructureKHR,
                .count = 1,
                .stages = vk::ShaderStageFlagBits::eRaygenKHR,
                .accel_structures = **tlas,
            },
            {
                .type = vk::DescriptorType::eStorageImage,
                .count = 1,
                .stages = vk::ShaderStageFlagBits::eRaygenKHR,
                .images =
                    {{
                        .images =
                            {
                                *offscreen_frames[0].image_view,
                                *offscreen_frames[1].image_view,
                            },
                        .layout = vk::ImageLayout::eGeneral,
                    }},
            },
        }, {
            UBO<UniformBufferObject>(vk::ShaderStageFlagBits::eRaygenKHR),
        }},
        .push_constants =
            {
                PushConstant<glm::mat4>(vk::ShaderStageFlagBits::eRaygenKHR),
            },
    });
    // clang-format on
}

VulkanPathTracerHW::UniformBufferObject VulkanPathTracerHW::GetUniformBufferObject() const {
    static auto startTime = std::chrono::high_resolution_clock::now();

    const auto currentTime = std::chrono::high_resolution_clock::now();
    const float time =
        std::chrono::duration<float, std::chrono::seconds::period>(currentTime - startTime).count();

    auto proj = glm::perspective(
        glm::radians(45.0f),
        swap_chain->extent.width / static_cast<float>(swap_chain->extent.height), 0.1f, 10.0f);
    proj[1][1] *= -1;
    return {
        .model =
            glm::rotate(glm::mat4(1.0f), time * glm::radians(90.0f), glm::vec3(0.0f, 0.0f, 1.0f)),
        .view = glm::lookAt(glm::vec3(2.0f, 2.0f, 2.0f), glm::vec3(0.0f, 0.0f, 0.0f),
                            glm::vec3(0.0f, 0.0f, 1.0f)),
        .proj = proj,
    };
}

const FrameInFlight& VulkanPathTracerHW::DrawFrameOffscreen() {
    const auto& frame = pipeline->AcquireNextFrame();
    pipeline->WriteUniformObject<UniformBufferObject>({GetUniformBufferObject()});

    pipeline->BeginFrame();
    frame.command_buffer.traceRaysKHR(pipeline->rgen_region, pipeline->miss_region,
                                      pipeline->hit_region, pipeline->call_region,
                                      swap_chain->extent.width, swap_chain->extent.height, 1);
    pipeline->EndFrame();

    device->graphics_queue.submit(
        {
            {
                .commandBufferCount = 1,
                .pCommandBuffers = TempArr<vk::CommandBuffer>{*frame.command_buffer},
                .signalSemaphoreCount = 1,
                .pSignalSemaphores = TempArr<vk::Semaphore>{*frame.render_finished_semaphore},
            },
        },
        *frame.in_flight_fence);
    return frame;
}

void VulkanPathTracerHW::OnResized(const vk::Extent2D& actual_extent) {
    VulkanRenderer::OnResized(actual_extent);
    pipeline->UpdateDescriptor(0, 1,
                               {
                                   .type = vk::DescriptorType::eStorageImage,
                                   .count = 1,
                                   .stages = vk::ShaderStageFlagBits::eRaygenKHR,
                                   .images = {{
                                       .images =
                                           {
                                               *offscreen_frames[0].image_view,
                                               *offscreen_frames[1].image_view,
                                           },
                                       .layout = vk::ImageLayout::eGeneral,
                                   }},
                               });
}

} // namespace Renderer
