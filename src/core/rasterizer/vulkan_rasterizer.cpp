// Copyright 2022 Pengfei Zhu
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <array>
#include <glm/glm.hpp>
#include "common/file_util.h"
#include "common/ranges.h"
#include "common/temp_ptr.h"
#include "core/rasterizer/vulkan_rasterizer.h"
#include "core/scene.h"
#include "core/vulkan/vulkan_allocator.h"
#include "core/vulkan/vulkan_buffer.h"
#include "core/vulkan/vulkan_context.h"
#include "core/vulkan/vulkan_descriptor_sets.h"
#include "core/vulkan/vulkan_frames_in_flight.hpp"
#include "core/vulkan/vulkan_graphics_pipeline.h"
#include "core/vulkan/vulkan_helpers.hpp"
#include "core/vulkan/vulkan_shader.h"
#include "core/vulkan/vulkan_swapchain.h"
#include "core/vulkan/vulkan_texture.h"

namespace Renderer {

VulkanRasterizer::VulkanRasterizer(bool enable_validation_layers,
                                   std::vector<const char*> frontend_required_extensions)
    : VulkanRenderer(enable_validation_layers, std::move(frontend_required_extensions)) {}

VulkanRasterizer::~VulkanRasterizer() {
    (*device)->waitIdle();
}

VulkanRenderer::OffscreenImageInfo VulkanRasterizer::GetOffscreenImageInfo() const {
    return {
        .format = swap_chain->surface_format.format,
        .usage = vk::ImageUsageFlagBits::eColorAttachment,
        .dst_stage_mask = vk::PipelineStageFlagBits2::eColorAttachmentOutput,
        .dst_access_mask = vk::AccessFlagBits2::eColorAttachmentWrite,
    };
}

std::unique_ptr<VulkanDevice> VulkanRasterizer::CreateDevice(
    vk::SurfaceKHR surface, [[maybe_unused]] const vk::Extent2D& actual_extent) const {
    return std::make_unique<VulkanDevice>(
        context->instance, surface,
        std::array{
            VK_KHR_SWAPCHAIN_EXTENSION_NAME,
            VK_EXT_VERTEX_INPUT_DYNAMIC_STATE_EXTENSION_NAME,
            VK_EXT_ROBUSTNESS_2_EXTENSION_NAME,
            VK_EXT_INDEX_TYPE_UINT8_EXTENSION_NAME,
        },
        Helpers::GenericStructureChain{vk::PhysicalDeviceFeatures2{
                                           .features =
                                               {
                                                   .samplerAnisotropy = VK_TRUE,
                                               },
                                       },
                                       // We don't need this in itself, but we enabled it on VMA
                                       vk::PhysicalDeviceVulkan12Features{
                                           .bufferDeviceAddress = VK_TRUE,
                                       },
                                       vk::PhysicalDeviceVulkan13Features{
                                           .pipelineCreationCacheControl = VK_TRUE,
                                           .synchronization2 = VK_TRUE,
                                       },
                                       vk::PhysicalDeviceVertexInputDynamicStateFeaturesEXT{
                                           .vertexInputDynamicState = VK_TRUE,
                                       },
                                       vk::PhysicalDeviceRobustness2FeaturesEXT{
                                           .nullDescriptor = VK_TRUE,
                                       },
                                       vk::PhysicalDeviceIndexTypeUint8FeaturesEXT{
                                           .indexTypeUint8 = VK_TRUE,
                                       }});
}

static vk::Format FindDepthFormat(const vk::raii::PhysicalDevice& physical_device) {
    static constexpr std::array<vk::Format, 3> Candidates{{
        vk::Format::eD32Sfloat,
        vk::Format::eD32SfloatS8Uint,
        vk::Format::eD24UnormS8Uint,
    }};

    for (const auto format : Candidates) {
        if (physical_device.getFormatProperties(format).optimalTilingFeatures &
            vk::FormatFeatureFlagBits::eDepthStencilAttachment) {
            return format;
        }
    }
    throw std::runtime_error("Failed to find depth format!");
}

void VulkanRasterizer::CreateDepthResources() {
    depth_image =
        std::make_unique<VulkanImage>(*device->allocator,
                                      vk::ImageCreateInfo{
                                          .imageType = vk::ImageType::e2D,
                                          .format = depth_format,
                                          .extent =
                                              {
                                                  .width = swap_chain->extent.width,
                                                  .height = swap_chain->extent.height,
                                                  .depth = 1,
                                              },
                                          .mipLevels = 1,
                                          .arrayLayers = 1,
                                          .usage = vk::ImageUsageFlagBits::eDepthStencilAttachment,
                                          .initialLayout = vk::ImageLayout::eUndefined,
                                      },
                                      VmaAllocationCreateInfo{
                                          .flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT,
                                          .usage = VMA_MEMORY_USAGE_AUTO,
                                          .priority = 1.0f,
                                      });
    depth_image_view =
        vk::raii::ImageView{**device,
                            {
                                .image = **depth_image,
                                .viewType = vk::ImageViewType::e2D,
                                .format = depth_format,
                                .subresourceRange =
                                    {
                                        .aspectMask = vk::ImageAspectFlagBits::eDepth,
                                        .baseMipLevel = 0,
                                        .levelCount = 1,
                                        .baseArrayLayer = 0,
                                        .layerCount = 1,
                                    },
                            }};
}

void VulkanRasterizer::Init(vk::SurfaceKHR surface, const vk::Extent2D& actual_extent) {
    VulkanRenderer::Init(surface, actual_extent);

    frames = std::make_unique<VulkanFramesInFlight<Frame, 2>>(*device);
    depth_format = FindDepthFormat(device->physical_device);

    // Pipeline
    render_pass =
        vk::raii::RenderPass{
            **device,
            {
                .attachmentCount = 2,
                .pAttachments =
                    TempArr<vk::AttachmentDescription>{
                        {
                            .format = swap_chain->surface_format.format,
                            .loadOp = vk::AttachmentLoadOp::eClear,
                            .storeOp = vk::AttachmentStoreOp::eStore,
                            .stencilLoadOp = vk::AttachmentLoadOp::eDontCare,
                            .stencilStoreOp = vk::AttachmentStoreOp::eDontCare,
                            .finalLayout = vk::ImageLayout::eGeneral,
                        },
                        {
                            .format = depth_format,
                            .loadOp = vk::AttachmentLoadOp::eClear,
                            .storeOp = vk::AttachmentStoreOp::eDontCare,
                            .stencilLoadOp = vk::AttachmentLoadOp::eDontCare,
                            .stencilStoreOp = vk::AttachmentStoreOp::eDontCare,
                            .finalLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal,
                        },
                    },
                .subpassCount = 1,
                .pSubpasses = TempArr<vk::SubpassDescription>{{
                    .pipelineBindPoint = vk::PipelineBindPoint::eGraphics,
                    .colorAttachmentCount = 1,
                    .pColorAttachments = TempArr<vk::AttachmentReference>{{
                        .attachment = 0,
                        .layout = vk::ImageLayout::eGeneral,
                    }},
                    .pDepthStencilAttachment = TempArr<vk::AttachmentReference>{{
                        .attachment = 1,
                        .layout = vk::ImageLayout::eDepthStencilAttachmentOptimal,
                    }},
                }},
                .dependencyCount = 2,
                .pDependencies =
                    TempArr<vk::SubpassDependency>{
                        {
                            .srcSubpass = VK_SUBPASS_EXTERNAL,
                            .dstSubpass = 0,
                            .srcStageMask = vk::PipelineStageFlagBits::eEarlyFragmentTests |
                                            vk::PipelineStageFlagBits::eLateFragmentTests,
                            .dstStageMask = vk::PipelineStageFlagBits::eEarlyFragmentTests |
                                            vk::PipelineStageFlagBits::eLateFragmentTests,
                            .srcAccessMask = vk::AccessFlagBits::eDepthStencilAttachmentRead |
                                             vk::AccessFlagBits::eDepthStencilAttachmentWrite,
                            .dstAccessMask = vk::AccessFlagBits::eDepthStencilAttachmentRead |
                                             vk::AccessFlagBits::eDepthStencilAttachmentWrite,
                        },
                        {
                            .srcSubpass = VK_SUBPASS_EXTERNAL,
                            .dstSubpass = 0,
                            .srcStageMask = vk::PipelineStageFlagBits::eNone,
                            .dstStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput,
                            .srcAccessMask = vk::AccessFlags{},
                            .dstAccessMask = vk::AccessFlagBits::eColorAttachmentWrite,
                        },
                    },
            }};

    CreateDepthResources();
    CreateFramebuffers();
}

void VulkanRasterizer::LoadScene(GLTF::Container& gltf) {
    scene = std::make_unique<Scene>();

    SceneLoader loader{{
                           .usage = vk::BufferUsageFlagBits::eVertexBuffer,
                           .dst_stage_mask = vk::PipelineStageFlagBits2::eVertexAttributeInput,
                           .dst_access_mask = vk::AccessFlagBits2::eVertexAttributeRead,
                       },
                       {
                           .usage = vk::BufferUsageFlagBits::eIndexBuffer,
                           .dst_stage_mask = vk::PipelineStageFlagBits2::eIndexInput,
                           .dst_access_mask = vk::AccessFlagBits2::eIndexRead,
                       },
                       *scene,
                       *device,
                       gltf};

    // Add a default material
    scene->materials.emplace_back(
        std::make_unique<Material>("Default", GLSL::Material{
                                                  .base_color_factor = glm::vec4{1, 1, 1, 1},
                                                  .base_color_texture_index = -1,
                                                  .metallic_factor = 1.0,
                                                  .roughness_factor = 1.0,
                                                  .metallic_roughness_texture_index = -1,
                                                  .normal_texture_index = -1,
                                                  .occlusion_texture_index = -1,
                                                  .emissive_texture_index = -1,
                                                  .emissive_factor = glm::vec3{},
                                              }));

    materials = Common::VectorFromRange(
        scene->materials | std::views::transform([this](const std::unique_ptr<Material>& material) {
            return std::make_unique<VulkanImmUploadBuffer>(
                *device,
                VulkanBufferCreateInfo{
                    .size = sizeof(Material),
                    .usage = vk::BufferUsageFlagBits::eUniformBuffer,
                    .dst_stage_mask = vk::PipelineStageFlagBits2::eFragmentShader,
                    .dst_access_mask = vk::AccessFlagBits2::eUniformRead,
                },
                reinterpret_cast<const u8*>(&material->glsl_material));
        }));

    descriptor_sets = std::make_unique<VulkanDescriptorSets>(
        *device, materials.size(),
        std::initializer_list<DescriptorBinding>{
            {
                .type = vk::DescriptorType::eUniformBuffer,
                .stages = vk::ShaderStageFlagBits::eFragment,
                .value = Common::VectorFromRange(
                    materials |
                    std::views::transform([](const std::unique_ptr<VulkanImmUploadBuffer>& buffer) {
                        return DescriptorBinding::Buffers{
                            .buffers = {{**buffer}},
                        };
                    })),
            },
            {
                .type = vk::DescriptorType::eCombinedImageSampler,
                .stages = vk::ShaderStageFlagBits::eFragment,
                .value = Common::VectorFromRange(
                    scene->materials |
                    std::views::transform([this](const std::unique_ptr<Material>& material) {
                        if (material->glsl_material.base_color_texture_index == -1) {
                            return DescriptorBinding::CombinedImageSamplers{
                                .images = {{
                                    .image = VK_NULL_HANDLE,
                                }},
                            };
                        }

                        const auto& texture =
                            scene->textures[material->glsl_material.base_color_texture_index];
                        return DescriptorBinding::CombinedImageSamplers{
                            .images = {{
                                .image = *texture->image->texture->image_view,
                                .sampler = texture->sampler ? *texture->sampler->sampler
                                                            : *device->default_sampler,
                            }},
                        };
                    })),
            },
        });

    pipeline = std::make_unique<VulkanGraphicsPipeline>(
        *device,
        vk::GraphicsPipelineCreateInfo{
            .stageCount = 2,
            .pStages = TempArr<vk::PipelineShaderStageCreateInfo>{{
                {
                    .stage = vk::ShaderStageFlagBits::eVertex,
                    .module = *VulkanShader{**device, u8"core/rasterizer/shaders/rasterizer.vert"},
                    .pName = "main",
                },
                {
                    .stage = vk::ShaderStageFlagBits::eFragment,
                    .module = *VulkanShader{**device, u8"core/rasterizer/shaders/rasterizer.frag"},
                    .pName = "main",
                },
            }},
            .pDepthStencilState = TempPtr{vk::PipelineDepthStencilStateCreateInfo{
                .depthTestEnable = VK_TRUE,
                .depthWriteEnable = VK_TRUE,
                .depthCompareOp = vk::CompareOp::eLess,
            }},
            .pDynamicState = TempPtr{vk::PipelineDynamicStateCreateInfo{
                .dynamicStateCount = 1,
                .pDynamicStates = TempArr<vk::DynamicState>{{vk::DynamicState::eVertexInputEXT}},
            }},
            .renderPass = *render_pass,
        },
        vk::PipelineLayoutCreateInfo{
            .setLayoutCount = 1,
            .pSetLayouts = &*descriptor_sets->descriptor_set_layout,
            .pushConstantRangeCount = 1,
            .pPushConstantRanges = TempArr<vk::PushConstantRange>{{
                PushConstant<glm::mat4>(vk::ShaderStageFlagBits::eVertex),
            }},
        });
}

void VulkanRasterizer::DrawFrame() {
    device->allocator->CleanupStagingBuffers();

    const auto& frame = frames->AcquireNextFrame();
    frames->BeginFrame();

    const auto& camera = scene->main_sub_scene->cameras[0];

    vk::Extent2D render_extent = swap_chain->extent;
    const double viewport_aspect_ratio =
        static_cast<double>(swap_chain->extent.width) / swap_chain->extent.height;
    const double camera_aspect_ratio = camera->GetAspectRatio(viewport_aspect_ratio);
    const double relative_aspect_ratio = viewport_aspect_ratio / camera_aspect_ratio;
    if (relative_aspect_ratio > 1) {
        render_extent.width = static_cast<u32>(render_extent.width / relative_aspect_ratio);
    } else {
        render_extent.height = static_cast<u32>(render_extent.height * relative_aspect_ratio);
    }

    const auto camera_transform = camera->GetProj(viewport_aspect_ratio) * camera->view;

    const auto& cmd = frame.command_buffer;
    pipeline->BeginRenderPass(cmd, {
                                       .framebuffer = *frame.extras.framebuffer,
                                       .renderArea =
                                           {
                                               .extent = render_extent,
                                           },
                                       .clearValueCount = 2,
                                       .pClearValues =
                                           TempArr<vk::ClearValue>{
                                               {.color = {{{0.0f, 0.0f, 0.0f, 0.0f}}}},
                                               {.depthStencil = {1.0f, 0}},
                                           },
                                   });
    cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, **pipeline);

    // TODO: Many optimization opportunities
    for (const auto& [mesh, model_transform] : scene->main_sub_scene->mesh_instances) {
        cmd.pushConstants<glm::mat4>(*pipeline->pipeline_layout, vk::ShaderStageFlagBits::eVertex,
                                     0, camera_transform * model_transform);
        for (const auto& primitive : mesh->primitives) {
            const std::size_t material =
                primitive->material == -1 ? materials.size() - 1 : primitive->material;
            cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *pipeline->pipeline_layout, 0,
                                   descriptor_sets->descriptor_sets[material], {});

            cmd.setVertexInputEXT(primitive->bindings, primitive->attributes);
            cmd.bindVertexBuffers(0, primitive->raw_vertex_buffers,
                                  primitive->vertex_buffer_offsets);
            cmd.bindIndexBuffer(**primitive->index_buffer->gpu_buffer, 0,
                                GLTF::GetIndexType(primitive->index_buffer->component_type));

            cmd.drawIndexed(static_cast<u32>(primitive->index_buffer->count), 1, 0, 0, 0);
        }
    }

    pipeline->EndRenderPass(cmd);

    frames->EndFrame();

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

    PostprocessAndPresent(*frame.render_finished_semaphore);
}

void VulkanRasterizer::CreateFramebuffers() {
    for (std::size_t i = 0; i < frames->frames_in_flight.size(); ++i) {
        frames->frames_in_flight[i].extras.framebuffer = vk::raii::Framebuffer{
            **device,
            vk::FramebufferCreateInfo{
                .renderPass = *render_pass,
                .attachmentCount = 2,
                .pAttachments =
                    TempArr<vk::ImageView>{*pp_frames->frames_in_flight[i].extras.image_view,
                                           *depth_image_view},
                .width = swap_chain->extent.width,
                .height = swap_chain->extent.height,
                .layers = 1,
            }};
    }
}

void VulkanRasterizer::OnResized([[maybe_unused]] const vk::Extent2D& actual_extent) {
    VulkanRenderer::OnResized(actual_extent);
    CreateDepthResources();
    CreateFramebuffers();
}

} // namespace Renderer
