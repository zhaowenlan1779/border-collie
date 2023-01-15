// Copyright 2022 Pengfei Zhu
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <array>
#include <unordered_map>
#include <glm/glm.hpp>
#include <spdlog/spdlog.h>
#include "common/assert.h"
#include "common/file_util.h"
#include "common/ranges.h"
#include "core/path_tracer_hw/shaders/path_tracer_glsl.h"
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
            VK_KHR_SHADER_CLOCK_EXTENSION_NAME,
        },
        Helpers::GenericStructureChain{vk::PhysicalDeviceFeatures2{
                                           .features =
                                               {
                                                   .samplerAnisotropy = VK_TRUE,
                                                   .shaderInt64 = VK_TRUE,
                                                   .shaderInt16 = VK_TRUE,
                                               },
                                       },
                                       vk::PhysicalDeviceVulkan11Features{
                                           .storageBuffer16BitAccess = VK_TRUE,
                                       },
                                       vk::PhysicalDeviceVulkan12Features{
                                           .storageBuffer8BitAccess = VK_TRUE,
                                           .shaderInt8 = VK_TRUE,
                                           .runtimeDescriptorArray = VK_TRUE,
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
                                       },
                                       vk::PhysicalDeviceShaderClockFeaturesKHR{
                                           .shaderSubgroupClock = VK_TRUE,
                                       }});
}

void VulkanPathTracerHW::Init(vk::SurfaceKHR surface, const vk::Extent2D& actual_extent) {
    VulkanRenderer::Init(surface, actual_extent);
    // TODO: Use a uniform instead and remove this requirement
    if (device->physical_device.getProperties().limits.maxPushConstantsSize <
        sizeof(GLSL::PathTracerPushConstantBlock)) {

        SPDLOG_ERROR("Physical device max push constants size is too small");
        throw std::runtime_error("Max push constants size too small");
    }
}

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

    // Upload primitives & build acceleration structures
    blases.clear();

    // Mesh ptr -> starting position in array
    std::unordered_map<std::shared_ptr<Mesh>, std::size_t> mesh_blas_map;
    std::vector<GLSL::PrimitiveInfo> primitives_info;
    for (const auto& [mesh, transform] : scene->main_sub_scene->mesh_instances) {
        if (mesh_blas_map.count(mesh)) {
            continue;
        }
        mesh_blas_map.emplace(mesh, blases.size());

        for (const auto& primitive : mesh->primitives) {
            const auto GetAttributeAddress = [this, &primitive](std::size_t i) -> u64 {
                const auto& attribute = primitive->attributes[i];
                if (!primitive->raw_vertex_buffers[attribute.binding]) {
                    return 0;
                }
                return (*device)->getBufferAddress({
                           .buffer = primitive->raw_vertex_buffers[attribute.binding],
                       }) +
                       primitive->vertex_buffer_offsets[attribute.binding] + attribute.offset;
            };
            const auto GetAttributeStride = [this, &primitive](std::size_t i) {
                const auto& attribute = primitive->attributes[i];
                return primitive->bindings[attribute.binding].stride;
            };

            // Location 0 is POSITION
            vk::AccelerationStructureGeometryKHR geometry{
                .geometryType = vk::GeometryTypeKHR::eTriangles,
                .geometry =
                    {
                        .triangles =
                            {
                                .vertexFormat = primitive->attributes[0].format,
                                .vertexData =
                                    {
                                        .deviceAddress = GetAttributeAddress(0),
                                    },
                                .vertexStride = GetAttributeStride(0),
                                .maxVertex = static_cast<u32>(primitive->max_vertices),
                            },
                    },
                .flags = vk::GeometryFlagBitsKHR::eOpaque,
            };
            if (primitive->index_buffer) {
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

            // Gather primitive info
            primitives_info.emplace_back(GLSL::PrimitiveInfo{
                .index_address = (*device)->getBufferAddress({
                    .buffer = **primitive->index_buffer->gpu_buffer,
                }),
                .position_address = GetAttributeAddress(0),
                .normal_address = GetAttributeAddress(1),
                .texcoord0_address = GetAttributeAddress(2),
                .texcoord1_address = GetAttributeAddress(3),
                .color_address = GetAttributeAddress(4),
                .tangent_address = GetAttributeAddress(5),
                .material_idx = primitive->material,
                .index_size = primitive->index_buffer
                                  ? static_cast<u32>(
                                        GetComponentSize(primitive->index_buffer->component_type))
                                  : 0,
                .position_stride = GetAttributeStride(0),
                .normal_stride = GetAttributeStride(1),
                .texcoord0_stride = GetAttributeStride(2),
                .texcoord0_type = GetTexCoordType(primitive->attributes[2].format),
                .texcoord1_stride = GetAttributeStride(3),
                .texcoord1_type = GetTexCoordType(primitive->attributes[3].format),
                .color_stride = GetAttributeStride(4),
                .color_type = GetColorType(primitive->attributes[4].format),
                .tangent_stride = GetAttributeStride(5),
            });

            // Try to compact and cleanup all previous BLASes
            for (auto& blas : blases) {
                blas->Compact();
                blas->Cleanup();
            }
        }
    }

    primitives_buffer = std::make_unique<VulkanImmUploadBuffer>(
        *device,
        VulkanBufferCreateInfo{
            .size = primitives_info.size() * sizeof(GLSL::PrimitiveInfo),
            .usage = vk::BufferUsageFlagBits::eStorageBuffer,
            .dst_stage_mask = vk::PipelineStageFlagBits2::eRayTracingShaderKHR,
            .dst_access_mask = vk::AccessFlagBits2::eShaderStorageRead,
        },
        reinterpret_cast<const u8*>(primitives_info.data()));

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
                .custom_index = static_cast<u32>(index + i),
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

    // Upload materials
    const auto materials_info = Common::VectorFromRange(
        scene->materials | std::views::transform([](const std::unique_ptr<Material>& material) {
            return material->glsl_material;
        }));
    materials_buffer = std::make_unique<VulkanImmUploadBuffer>(
        *device,
        VulkanBufferCreateInfo{
            .size = materials_info.size() * sizeof(GLSL::Material),
            .usage = vk::BufferUsageFlagBits::eStorageBuffer,
            .dst_stage_mask = vk::PipelineStageFlagBits2::eRayTracingShaderKHR,
            .dst_access_mask = vk::AccessFlagBits2::eShaderStorageRead,
        },
        reinterpret_cast<const u8*>(materials_info.data()));

    frames = std::make_unique<VulkanFramesInFlight<Frame, 2>>(*device);

    auto images = Common::VectorFromRange(
        scene->textures | std::views::transform([this](const std::unique_ptr<Texture>& texture) {
            return DescriptorBinding::CombinedImageSampler{
                .image = *texture->image->texture->image_view,
                .sampler = texture->sampler ? *texture->sampler->sampler : *device->default_sampler,
            };
        }));
    if (images.empty()) {
        // Cannot create empty descriptor, and also cannot bind null in raytracing, so we'll have
        // to get ourselves a real texture
        error_texture = std::make_unique<VulkanTexture>(
            *device, Common::ReadFileContents(u8"textures/texture.jpg"));
        images.emplace_back(DescriptorBinding::CombinedImageSampler{
            .image = *error_texture->image_view,
            .sampler = *device->default_sampler,
        });
    }

    fixed_descriptor_set = std::make_unique<VulkanDescriptorSets>(
        *device, 1,
        std::initializer_list<DescriptorBinding>{
            {
                .type = vk::DescriptorType::eAccelerationStructureKHR,
                .stages = vk::ShaderStageFlagBits::eRaygenKHR,
                .value = DescriptorBinding::AccelStructuresValue{{
                    .accel_structures = {{**tlas}},
                }},
            },
            {
                .type = vk::DescriptorType::eStorageBuffer,
                .stages = vk::ShaderStageFlagBits::eClosestHitKHR,
                .value = DescriptorBinding::BuffersValue{{
                    .buffers = {{**primitives_buffer}},
                }},
            },
            {
                .type = vk::DescriptorType::eStorageBuffer,
                .stages = vk::ShaderStageFlagBits::eClosestHitKHR,
                .value = DescriptorBinding::BuffersValue{{
                    .buffers = {{**materials_buffer}},
                }},
            },
            {
                .type = vk::DescriptorType::eCombinedImageSampler,
                .array_size = static_cast<u32>(images.size()),
                .stages = vk::ShaderStageFlagBits::eClosestHitKHR,
                .value = DescriptorBinding::CombinedImageSamplersValue{{
                    .images = images,
                }},
            }});

    image_descriptor_sets = std::make_unique<VulkanDescriptorSets>(
        *device, 2,
        std::initializer_list<DescriptorBinding>{
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
            .setLayoutCount = 2,
            .pSetLayouts = TempArr<vk::DescriptorSetLayout>{{
                *fixed_descriptor_set->descriptor_set_layout,
                *image_descriptor_sets->descriptor_set_layout,
            }},
            .pushConstantRangeCount = 1,
            .pPushConstantRanges = TempArr<vk::PushConstantRange>{{
                PushConstant<GLSL::PathTracerPushConstantBlock>(
                    vk::ShaderStageFlagBits::eRaygenKHR | vk::ShaderStageFlagBits::eClosestHitKHR |
                    vk::ShaderStageFlagBits::eMissKHR),
            }},
        });
}

void VulkanPathTracerHW::DrawFrame(const Camera& external_camera, bool force_external_camera) {
    device->allocator->CleanupStagingBuffers();

    const auto& frame = frames->AcquireNextFrame();

    frames->BeginFrame();

    const auto& cmd = frame.command_buffer;
    cmd.bindPipeline(vk::PipelineBindPoint::eRayTracingKHR, **pipeline);
    cmd.bindDescriptorSets(vk::PipelineBindPoint::eRayTracingKHR, *pipeline->pipeline_layout, 0,
                           {fixed_descriptor_set->descriptor_sets[0],
                            image_descriptor_sets->descriptor_sets[frame.idx]},
                           {});

    const bool use_external_camera =
        force_external_camera || scene->main_sub_scene->cameras.empty();
    const auto& camera = use_external_camera ? external_camera : *scene->main_sub_scene->cameras[0];
    const double viewport_aspect_ratio =
        static_cast<double>(swap_chain->extent.width) / swap_chain->extent.height;
    const auto render_extent = GetRenderExtent(camera.GetAspectRatio(viewport_aspect_ratio));

    const auto& view = camera.view;
    const auto& proj = camera.GetProj(viewport_aspect_ratio);
    if (view != last_camera_view || proj != last_camera_proj) {
        frame_count = 0;
    }

    last_camera_view = view;
    last_camera_proj = proj;
    cmd.pushConstants<GLSL::PathTracerPushConstantBlock>(
        *pipeline->pipeline_layout,
        vk::ShaderStageFlagBits::eRaygenKHR | vk::ShaderStageFlagBits::eClosestHitKHR |
            vk::ShaderStageFlagBits::eMissKHR,
        0,
        {{{
            .view_inverse = glm::inverse(view),
            .proj_inverse = glm::inverse(proj),
            .intensity_multiplier = intensity_multiplier,
            .ambient_light = ambient_light,
            .frame = frame_count++,
        }}});
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
    image_descriptor_sets->UpdateDescriptor(
        0, DescriptorBinding::CombinedImageSamplersValue{
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
    frame_count = 0;
}

void VulkanPathTracerHW::SetLightProperties(float multiplier_, float ambient_light_) {
    intensity_multiplier = multiplier_;
    ambient_light = ambient_light_;
}

} // namespace Renderer
