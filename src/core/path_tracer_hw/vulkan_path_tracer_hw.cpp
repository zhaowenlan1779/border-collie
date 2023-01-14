// Copyright 2022 Pengfei Zhu
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <array>
#include <unordered_map>
#include <glm/glm.hpp>
#include <spdlog/spdlog.h>
#include "common/ranges.h"
#include "core/path_tracer_hw/vulkan_path_tracer_hw.h"
#include "core/scene.h"
#include "core/vulkan/vulkan_accel_structure.h"
#include "core/vulkan/vulkan_allocator.h"
#include "core/vulkan/vulkan_buffer.h"
#include "core/vulkan/vulkan_context.h"
#include "core/vulkan/vulkan_descriptor_sets.h"
#include "core/vulkan/vulkan_device.h"
#include "core/vulkan/vulkan_frames_in_flight.hpp"
#include "core/vulkan/vulkan_helpers.hpp"
#include "core/vulkan/vulkan_raytracing_pipeline.h"
#include "core/vulkan/vulkan_shader.h"
#include "core/vulkan/vulkan_swapchain.h"
#include "core/vulkan/vulkan_texture.h"

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

void VulkanPathTracerHW::Init(vk::SurfaceKHR surface, const vk::Extent2D& actual_extent) {
    VulkanRenderer::Init(surface, actual_extent);
}

struct PathTracerPushConstant {
    glm::mat4 view_inverse;
    glm::mat4 proj_inverse;
};

void VulkanPathTracerHW::LoadScene(GLTF::Container& gltf) {
    scene = std::make_unique<Scene>();

    SceneLoader loader{
        {
            .usage = vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR |
                     vk::BufferUsageFlagBits::eShaderDeviceAddress,
            .dst_stage_mask = vk::PipelineStageFlagBits2::eAccelerationStructureBuildKHR,
            .dst_access_mask = vk::AccessFlagBits2::eShaderRead,
        },
        {
            .usage = vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR |
                     vk::BufferUsageFlagBits::eShaderDeviceAddress,
            .dst_stage_mask = vk::PipelineStageFlagBits2::eAccelerationStructureBuildKHR,
            .dst_access_mask = vk::AccessFlagBits2::eShaderRead,
        },
        *scene,
        *device,
        gltf};

    // Build acceleration structures
    blases.clear();

    // Mesh ptr -> starting position in array
    std::unordered_map<std::shared_ptr<Mesh>, std::size_t> mesh_blas_map;
    for (const auto& [mesh, transform] : scene->main_sub_scene->mesh_instances) {
        if (mesh_blas_map.count(mesh)) {
            continue;
        }
        mesh_blas_map.emplace(mesh, blases.size());

        for (const auto& primitive : mesh->primitives) {
            // Location 0 is POSITION
            const auto& attribute = primitive->attributes[0];
            vk::AccelerationStructureGeometryKHR geometry{
                .geometryType = vk::GeometryTypeKHR::eTriangles,
                .geometry =
                    {
                        .triangles =
                            {
                                .vertexFormat = attribute.format,
                                .vertexData =
                                    {
                                        .deviceAddress =
                                            (*device)->getBufferAddress({
                                                .buffer = primitive->raw_vertex_buffers[0],
                                            }) +
                                            primitive->vertex_buffer_offsets[0] + attribute.offset,
                                    },
                                .vertexStride = primitive->bindings[attribute.binding].stride,
                                .maxVertex = static_cast<u32>(primitive->max_vertices),
                            },
                    },
                .flags = vk::GeometryFlagBitsKHR::eOpaque,
            };
            if (primitive->index_buffer) {
                // TODO: Handle Uint8 indices
                geometry.geometry.triangles.indexType =
                    GLTF::GetIndexType(primitive->index_buffer->component_type);
                geometry.geometry.triangles.indexData = {
                    .deviceAddress = (*device)->getBufferAddress({
                        .buffer = **primitive->index_buffer->gpu_buffer,
                    }),
                };
                blases.emplace_back(std::make_unique<VulkanAccelStructure>(
                    *device, geometry,
                    vk::AccelerationStructureBuildRangeInfoKHR{
                        .primitiveCount = static_cast<u32>(primitive->index_buffer->count / 3),
                    }));
            } else {
                geometry.geometry.triangles.indexType = vk::IndexType::eNoneKHR;
                blases.emplace_back(std::make_unique<VulkanAccelStructure>(
                    *device, geometry,
                    vk::AccelerationStructureBuildRangeInfoKHR{
                        .primitiveCount = static_cast<u32>(primitive->max_vertices / 3),
                    }));
            }

            // Try to compact and cleanup all previous BLASes
            for (auto& blas : blases) {
                blas->Compact();
                blas->Cleanup();
            }
        }
    }

    // Wait until all compacts have started
    auto blases_to_compact = Common::VectorFromRange(
        blases | std::views::filter([](const auto& ptr) { return *ptr->build_fence; }) |
        std::views::transform(&std::unique_ptr<VulkanAccelStructure>::get));
    while (!blases_to_compact.empty()) {
        const auto result = (*device)->waitForFences(
            Common::VectorFromRange(blases_to_compact |
                                    std::views::transform([](VulkanAccelStructure* blas) {
                                        return *blas->build_fence;
                                    })),
            VK_FALSE, std::numeric_limits<u64>::max());
        if (result != vk::Result::eSuccess) {
            SPDLOG_ERROR("Failed to wait for fences");
            throw std::runtime_error("Failed to wait for fences");
        }

        std::vector<VulkanAccelStructure*> new_blases_to_compact;
        for (auto* blas : blases_to_compact) {
            if (blas->build_fence.getStatus() == vk::Result::eSuccess) {
                blas->Compact();
            } else {
                new_blases_to_compact.emplace_back(blas);
            }
        }
        blases_to_compact = std::move(new_blases_to_compact);

        for (auto& blas : blases) {
            blas->Cleanup();
        }
    }

    std::vector<VulkanAccelStructure::BLASInstance> instances;
    for (const auto& [mesh, transform] : scene->main_sub_scene->mesh_instances) {
        const std::size_t index = mesh_blas_map.at(mesh);
        for (std::size_t i = 0; i < mesh->primitives.size(); ++i) {
            instances.emplace_back(VulkanAccelStructure::BLASInstance{
                .blas = *blases.at(index + i),
                .transform = transform,
            });
        }
    }
    tlas = std::make_unique<VulkanAccelStructure>(instances);

    // Pending tasks: compact & cleanup TLAS; cleanup BLAS
    // Strictly speaking we do not have to cleanup everything here, but we do not want to maintain
    // the states
    auto blases_to_clean = Common::VectorFromRange(
        blases | std::views::filter([](const auto& ptr) { return *ptr->compact_fence; }) |
        std::views::transform(&std::unique_ptr<VulkanAccelStructure>::get));
    while (!blases_to_clean.empty() || *tlas->build_fence || *tlas->compact_fence) {
        auto fences = Common::VectorFromRange(
            blases_to_clean |
            std::views::transform([](VulkanAccelStructure* blas) { return *blas->compact_fence; }));
        if (*tlas->build_fence) { // compact has not started
            fences.emplace_back(*tlas->build_fence);
        } else if (*tlas->compact_fence) { // not cleaned up
            fences.emplace_back(*tlas->compact_fence);
        }
        const auto result =
            (*device)->waitForFences(fences, VK_FALSE, std::numeric_limits<u64>::max());
        if (result != vk::Result::eSuccess) {
            SPDLOG_ERROR("Failed to wait for fences");
            throw std::runtime_error("Failed to wait for fences");
        }

        tlas->Compact();
        tlas->Cleanup();

        std::vector<VulkanAccelStructure*> new_blases_to_clean;
        for (auto* blas : blases_to_clean) {
            if (blas->compact_fence.getStatus() == vk::Result::eSuccess) {
                blas->Cleanup();
            } else {
                new_blases_to_clean.emplace_back(blas);
            }
        }
        blases_to_clean = std::move(new_blases_to_clean);
    }

    frames = std::make_unique<VulkanFramesInFlight<Frame, 2>>(*device);

    descriptor_sets = std::make_unique<VulkanDescriptorSets>(
        *device, 2,
        std::initializer_list<DescriptorBinding>{
            {
                .type = vk::DescriptorType::eAccelerationStructureKHR,
                .stages = vk::ShaderStageFlagBits::eRaygenKHR,
                .value = DescriptorBinding::AccelStructuresValue{{
                    .accel_structures = {{**tlas}},
                }},
            },
            {
                .type = vk::DescriptorType::eStorageImage,
                .stages = vk::ShaderStageFlagBits::eRaygenKHR,
                .value =
                    DescriptorBinding::CombinedImageSamplersValue{
                        {
                            .images = {{
                                .image = *pp_frames->frames_in_flight[0].extras.image_view,
                                .layout = vk::ImageLayout::eGeneral,
                            }},
                        },
                        {
                            .images = {{
                                .image = *pp_frames->frames_in_flight[1].extras.image_view,
                                .layout = vk::ImageLayout::eGeneral,
                            }},
                        },
                    },
            },
        });

    pipeline = std::make_unique<VulkanRayTracingPipeline>(
        *device,
        vk::RayTracingPipelineCreateInfoKHR{
            .stageCount = 3,
            .pStages = TempArr<vk::PipelineShaderStageCreateInfo>{{
                {
                    .stage = vk::ShaderStageFlagBits::eRaygenKHR,
                    .module =
                        *VulkanShader{**device, u8"core/path_tracer_hw/shaders/raytrace.rgen"},
                    .pName = "main",
                },
                {
                    .stage = vk::ShaderStageFlagBits::eMissKHR,
                    .module =
                        *VulkanShader{**device, u8"core/path_tracer_hw/shaders/raytrace.rmiss"},
                    .pName = "main",
                },
                {
                    .stage = vk::ShaderStageFlagBits::eClosestHitKHR,
                    .module =
                        *VulkanShader{**device, u8"core/path_tracer_hw/shaders/raytrace.rchit"},
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
        vk::PipelineLayoutCreateInfo{
            .setLayoutCount = 1,
            .pSetLayouts = &*descriptor_sets->descriptor_set_layout,
            .pushConstantRangeCount = 1,
            .pPushConstantRanges = TempArr<vk::PushConstantRange>{{
                PushConstant<PathTracerPushConstant>(vk::ShaderStageFlagBits::eRaygenKHR),
            }},
        });
}

void VulkanPathTracerHW::DrawFrame() {
    device->allocator->CleanupStagingBuffers();

    const auto& frame = frames->AcquireNextFrame();

    frames->BeginFrame();

    const auto& cmd = frame.command_buffer;
    cmd.bindPipeline(vk::PipelineBindPoint::eRayTracingKHR, **pipeline);
    cmd.bindDescriptorSets(vk::PipelineBindPoint::eRayTracingKHR, *pipeline->pipeline_layout, 0,
                           descriptor_sets->descriptor_sets[frame.idx], {});

    const auto& camera = scene->main_sub_scene->cameras[0];
    const double viewport_aspect_ratio =
        static_cast<double>(swap_chain->extent.width) / swap_chain->extent.height;
    const auto render_extent = GetRenderExtent(camera->GetAspectRatio(viewport_aspect_ratio));

    cmd.pushConstants<PathTracerPushConstant>(
        *pipeline->pipeline_layout, vk::ShaderStageFlagBits::eRaygenKHR, 0,
        {{
            .view_inverse = glm::inverse(camera->view),
            .proj_inverse = glm::inverse(camera->GetProj(viewport_aspect_ratio)),
        }});
    pipeline->TraceRays(cmd, render_extent.width, render_extent.height, 1);

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
    descriptor_sets->UpdateDescriptor(
        1, DescriptorBinding::CombinedImageSamplersValue{
               {
                   .images = {{
                       .image = *pp_frames->frames_in_flight[0].extras.image_view,
                       .layout = vk::ImageLayout::eGeneral,
                   }},
               },
               {
                   .images = {{
                       .image = *pp_frames->frames_in_flight[1].extras.image_view,
                       .layout = vk::ImageLayout::eGeneral,
                   }},
               },
           });
}

} // namespace Renderer
