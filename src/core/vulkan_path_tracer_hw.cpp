// Copyright 2022 Pengfei Zhu
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <array>
#include <glm/gtc/matrix_transform.hpp>
#include "common/file_util.h"
#include "core/shaders/renderer_glsl.h"
#include "core/vulkan/vulkan_accel_structure.h"
#include "core/vulkan/vulkan_allocator.h"
#include "core/vulkan/vulkan_buffer.h"
#include "core/vulkan/vulkan_context.h"
#include "core/vulkan/vulkan_device.h"
#include "core/vulkan/vulkan_frames_in_flight.hpp"
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

    texture = std::make_unique<VulkanTexture>(*device,
                                              Common::ReadFileContents(u8"textures/texture.jpg"));

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

    frames = std::make_unique<VulkanFramesInFlight<Frame, 2>>(*device);
    for (auto& frame_in_flight : frames->frames_in_flight) {
        frame_in_flight.extras.uniform =
            std::make_unique<VulkanUniformBufferObject<GLSL::PathTracerUBOBlock>>(
                *device->allocator, vk::PipelineStageFlagBits2::eRayTracingShaderKHR);
    }
    frames->CreateDescriptors({{
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
            .images = {{
                .images =
                    {
                        *pp_frames->frames_in_flight[0].extras.image_view,
                        *pp_frames->frames_in_flight[1].extras.image_view,
                    },
                .layout = vk::ImageLayout::eGeneral,
            }},
        },
        {
            .type = vk::DescriptorType::eUniformBuffer,
            .count = 1,
            .stages = vk::ShaderStageFlagBits::eRaygenKHR,
            .buffers = {{
                {
                    *frames->frames_in_flight[0].extras.uniform->dst_buffer,
                    *frames->frames_in_flight[1].extras.uniform->dst_buffer,
                },
            }},
        },
    }});

    pipeline = std::make_unique<VulkanRayTracingPipeline>(
        *device,
        vk::RayTracingPipelineCreateInfoKHR{
            .stageCount = 3,
            .pStages = TempArr<vk::PipelineShaderStageCreateInfo>{{
                {
                    .stage = vk::ShaderStageFlagBits::eRaygenKHR,
                    .module = *VulkanShader{**device, u8"core/shaders/raytracing_hw/raytrace.rgen"},
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
        frames->CreatePipelineLayout({
            PushConstant<glm::mat4>(vk::ShaderStageFlagBits::eRaygenKHR),
        }));
}

GLSL::PathTracerUBOBlock VulkanPathTracerHW::GetUniformBufferObject() const {
    auto proj = glm::perspective(
        glm::radians(45.0f),
        swap_chain->extent.width / static_cast<float>(swap_chain->extent.height), 0.1f, 10.0f);
    proj[1][1] *= -1;
    return {{
        // .model =
        //    glm::rotate(glm::mat4(1.0f), time * glm::radians(90.0f), glm::vec3(0.0f, 0.0f, 1.0f)),
        .view_inverse = glm::inverse(glm::lookAt(
            glm::vec3(2.0f, 2.0f, 2.0f), glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f))),
        .proj_inverse = glm::inverse(proj),
    }};
}

void VulkanPathTracerHW::DrawFrame() {
    device->allocator->CleanupStagingBuffers();

    const auto& frame = frames->AcquireNextFrame();
    frame.extras.uniform->Update(GetUniformBufferObject());

    frames->BeginFrame();

    const auto& cmd = frame.command_buffer;
    frame.extras.uniform->Upload(cmd);
    cmd.bindPipeline(vk::PipelineBindPoint::eRayTracingKHR, **pipeline);
    cmd.bindDescriptorSets(vk::PipelineBindPoint::eRayTracingKHR, *pipeline->pipeline_layout, 0,
                           frames->GetDescriptorSets(), {});
    pipeline->TraceRays(cmd, swap_chain->extent.width, swap_chain->extent.height, 1);

    frames->EndFrame();

    device->graphics_queue.submit(
        {{
            .commandBufferCount = 1,
            .pCommandBuffers = TempArr<vk::CommandBuffer>{*frame.command_buffer},
            .signalSemaphoreCount = 1,
            .pSignalSemaphores = TempArr<vk::Semaphore>{*frame.render_finished_semaphore},
        }},
        *frame.in_flight_fence);
    PostprocessAndPresent(*frame.render_finished_semaphore);
}

void VulkanPathTracerHW::OnResized(const vk::Extent2D& actual_extent) {
    VulkanRenderer::OnResized(actual_extent);
    frames->UpdateDescriptor(0, 1,
                             {
                                 .type = vk::DescriptorType::eStorageImage,
                                 .count = 1,
                                 .stages = vk::ShaderStageFlagBits::eRaygenKHR,
                                 .images = {{
                                     .images =
                                         {
                                             *pp_frames->frames_in_flight[0].extras.image_view,
                                             *pp_frames->frames_in_flight[1].extras.image_view,
                                         },
                                     .layout = vk::ImageLayout::eGeneral,
                                 }},
                             });
}

} // namespace Renderer
