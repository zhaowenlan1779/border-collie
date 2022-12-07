// Copyright 2022 Pengfei Zhu
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <array>
#include <chrono>
#include <glm/gtc/matrix_transform.hpp>
#include <spdlog/spdlog.h>
#include "common/temp_ptr.h"
#include "core/renderer/vulkan_allocator.h"
#include "core/renderer/vulkan_buffer.h"
#include "core/renderer/vulkan_context.h"
#include "core/renderer/vulkan_device.h"
#include "core/renderer/vulkan_graphics_pipeline.h"
#include "core/renderer/vulkan_helpers.hpp"
#include "core/renderer/vulkan_renderer.h"
#include "core/renderer/vulkan_shader.h"
#include "core/renderer/vulkan_swapchain.h"
#include "core/renderer/vulkan_texture.h"

namespace Renderer {

VulkanRenderer::VulkanRenderer(bool enable_validation_layers,
                               std::vector<const char*> frontend_required_extensions) {

    context =
        std::make_unique<VulkanContext>(enable_validation_layers, frontend_required_extensions);
}

VulkanRenderer::~VulkanRenderer() {
    (*device)->waitIdle();
}

vk::raii::Instance& VulkanRenderer::GetVulkanInstance() {
    return context->instance;
}

const vk::raii::Instance& VulkanRenderer::GetVulkanInstance() const {
    return context->instance;
}

struct Vertex {
    glm::vec2 pos;
    glm::vec3 color;
    glm::vec2 texCoord;
};

struct VulkanRenderer::UniformBufferObject {
    glm::mat4 model;
    glm::mat4 view;
    glm::mat4 proj;
};

void VulkanRenderer::Init(vk::SurfaceKHR surface, const vk::Extent2D& actual_extent) {
    device = std::make_unique<VulkanDevice>(
        context->instance, surface,
        std::array{
            VK_KHR_SWAPCHAIN_EXTENSION_NAME,
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
                                       }});

    // Buffers & Textures
    const std::vector<Vertex> vertices = {{{-0.5f, -0.5f}, {1.0f, 0.0f, 0.0f}, {1.0f, 0.0f}},
                                          {{0.5f, -0.5f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f}},
                                          {{0.5f, 0.5f}, {0.0f, 0.0f, 1.0f}, {0.0f, 1.0f}},
                                          {{-0.5f, 0.5f}, {1.0f, 1.0f, 1.0f}, {1.0f, 1.0f}}};
    vertex_buffer = std::make_unique<VulkanImmUploadBuffer>(
        *device, VulkanImmUploadBufferCreateInfo{
                     .data = reinterpret_cast<const u8*>(vertices.data()),
                     .size = vertices.size() * sizeof(vertices[0]),
                     .usage = vk::BufferUsageFlagBits::eVertexBuffer,
                     .dst_stage_mask = vk::PipelineStageFlagBits2::eVertexAttributeInput,
                     .dst_access_mask = vk::AccessFlagBits2::eVertexAttributeRead,
                 });

    const std::vector<u16> indices = {0, 1, 2, 2, 3, 0};
    index_buffer = std::make_unique<VulkanImmUploadBuffer>(
        *device, VulkanImmUploadBufferCreateInfo{
                     .data = reinterpret_cast<const u8*>(indices.data()),
                     .size = indices.size() * sizeof(indices[0]),
                     .usage = vk::BufferUsageFlagBits::eIndexBuffer,
                     .dst_stage_mask = vk::PipelineStageFlagBits2::eIndexInput,
                     .dst_access_mask = vk::AccessFlagBits2::eIndexRead,
                 });

    texture = std::make_unique<VulkanTexture>(*device, u8"textures/texture.jpg");

    swap_chain = std::make_unique<VulkanSwapchain>(*device, actual_extent);

    // Pipeline
    static constexpr auto VertexAttributeDescriptions = Helpers::AttributeDescriptionsFor<Vertex>();

    // clang-format off
    pipeline = std::make_unique<VulkanGraphicsPipeline>(*device, VulkanGraphicsPipelineCreateInfo{
        .render_pass_info = {
            .attachmentCount = 1,
            .pAttachments = TempArr<vk::AttachmentDescription>{{
                .format = swap_chain->surface_format.format,
                .samples = vk::SampleCountFlagBits::e1,
                .loadOp = vk::AttachmentLoadOp::eClear,
                .storeOp = vk::AttachmentStoreOp::eStore,
                .stencilLoadOp = vk::AttachmentLoadOp::eDontCare,
                .stencilStoreOp = vk::AttachmentStoreOp::eDontCare,
                .finalLayout = vk::ImageLayout::ePresentSrcKHR,
            }},
            .subpassCount = 1,
            .pSubpasses = TempArr<vk::SubpassDescription>{{
                .pipelineBindPoint = vk::PipelineBindPoint::eGraphics,
                .colorAttachmentCount = 1,
                .pColorAttachments = TempArr<vk::AttachmentReference>{{
                    .attachment = 0,
                    .layout = vk::ImageLayout::eColorAttachmentOptimal,
                }},
            }},
            .dependencyCount = 1,
            .pDependencies = TempArr<vk::SubpassDependency>{{
                .srcSubpass = VK_SUBPASS_EXTERNAL,
                .dstSubpass = 0,
                .srcStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput,
                .dstStageMask = vk::PipelineStageFlagBits::eFragmentShader,
                .srcAccessMask = vk::AccessFlagBits::eColorAttachmentWrite,
                .dstAccessMask = vk::AccessFlagBits::eUniformRead,
            }},
        },
        .pipeline_info = {
            .stageCount = 2,
            .pStages = TempArr<vk::PipelineShaderStageCreateInfo>{{
                {
                    .stage = vk::ShaderStageFlagBits::eVertex,
                    .module = *VulkanShader{**device, u8"core/renderer/shaders/test.vert"},
                    .pName = "main",
                },
                {
                    .stage = vk::ShaderStageFlagBits::eFragment,
                    .module = *VulkanShader{**device, u8"core/renderer/shaders/test.frag"},
                    .pName = "main",
                },
            }},
            .pVertexInputState = TempPtr{vk::PipelineVertexInputStateCreateInfo{
                .vertexBindingDescriptionCount = 1,
                .pVertexBindingDescriptions = TempArr<vk::VertexInputBindingDescription>{{
                    .binding = 0,
                    .stride = sizeof(Vertex),
                    .inputRate = vk::VertexInputRate::eVertex,
                }},
                .vertexAttributeDescriptionCount = VertexAttributeDescriptions.size(),
                .pVertexAttributeDescriptions = VertexAttributeDescriptions.data(),
            }},
        },
        .descriptor_sets = {{
            UBO<UniformBufferObject>(vk::ShaderStageFlagBits::eVertex),
            {
                .type = vk::DescriptorType::eCombinedImageSampler,
                .count = 1,
                .stages = vk::ShaderStageFlagBits::eFragment,
                .images = {
                    {
                        .image = *texture->image_view,
                        .layout = vk::ImageLayout::eShaderReadOnlyOptimal,
                    },
                },
            },
        }},
        .push_constants = {
            PushConstant<glm::mat4>(vk::ShaderStageFlagBits::eVertex),
        },
    });
    // clang-format on

    swap_chain->CreateFramebuffers(pipeline->render_pass);

    // Assign buffers
    pipeline->vertex_buffers = {**vertex_buffer};
    pipeline->index_buffer = **index_buffer;
    pipeline->index_buffer_type = vk::IndexType::eUint16;
}

// void VulkanRenderer::CreateRenderTargets() {
//     Helpers::OneTimeCommandContext context{device, command_pool, graphics_queue};

//     for (auto& frame : frames_in_flight) {
//         frame.render_target =
//             std::make_unique<VulkanImage>(*allocator,
//                                           vk::ImageCreateInfo{
//                                               .imageType = vk::ImageType::e2D,
//                                               .format = vk::Format::eR8G8B8A8Srgb,
//                                               .extent =
//                                                   {
//                                                       .width = extent.width,
//                                                       .height = extent.height,
//                                                       .depth = 1,
//                                                   },
//                                               .mipLevels = 1,
//                                               .arrayLayers = 1,
//                                               .usage = vk::ImageUsageFlagBits::eColorAttachment |
//                                                        vk::ImageUsageFlagBits::eSampled,
//                                               .initialLayout = vk::ImageLayout::eUndefined,
//                                           },
//                                           VmaAllocationCreateInfo{
//                                               .flags =
//                                               VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT, .usage
//                                               = VMA_MEMORY_USAGE_AUTO, .priority = 1.0f,
//                                           });

//         Helpers::ImageLayoutTransition(
//             *context, frame.render_target,
//             {
//                 .srcStageMask = vk::PipelineStageFlagBits2::eTopOfPipe,
//                 .srcAccessMask = vk::AccessFlags2{},
//                 .dstStageMask = vk::PipelineStageFlagBits2::eBottomOfPipe,
//                 .dstAccessMask = vk::AccessFlags2{},
//                 .oldLayout = vk::ImageLayout::eUndefined,
//                 .newLayout = vk::ImageLayout::eGeneral,
//                 .subresourceRange =
//                     {
//                         .baseMipLevel = 0,
//                         .levelCount = 1,
//                     },
//             });

//         frame.render_target_view =
//             vk::raii::ImageView{device,
//                                 {
//                                     .image = **frame.render_target,
//                                     .viewType = vk::ImageViewType::e2D,
//                                     .format = vk::Format::eR8G8B8A8Srgb,
//                                     .subresourceRange =
//                                         {
//                                             .aspectMask = vk::ImageAspectFlagBits::eColor,
//                                             .baseMipLevel = 0,
//                                             .levelCount = 1,
//                                             .baseArrayLayer = 0,
//                                             .layerCount = 1,
//                                         },
//                                 }};
//         frame.render_target_framebuffer = vk::raii::Framebuffer{
//             device, vk::FramebufferCreateInfo{
//                         .renderPass = *render_pass,
//                         .attachmentCount = 1,
//                         .pAttachments = TempArr<vk::ImageView>{*frame.render_target_view},
//                         .width = extent.width,
//                         .height = extent.height,
//                         .layers = 1,
//                     }};
//     }
// }

// void VulkanRenderer::RecordPostProcessCommands(FrameInFlight& frame, std::size_t image_index) {
//     const auto& cmd = frame.command_buffer;

//     Helpers::CommandBufferContext cmd_context{cmd, {}};
//     Helpers::CommandBufferRenderPassContext render_pass_context{
//         cmd,
//         {
//             .renderPass = *render_pass,
//             .framebuffer = *framebuffers[image_index],
//             .renderArea =
//                 {
//                     .offset = {},
//                     .extent = extent,
//                 },
//             .clearValueCount = 1,
//             .pClearValues =
//                 TempArr<vk::ClearValue>{
//                     {{{{0.0f, 0.0f, 0.0f, 1.0f}}}},
//                 },
//         },
//         vk::SubpassContents::eInline};

//     cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *pp_pipeline);
//     // Dynamic states
//     cmd.setViewport(0, {{
//                            .x = 0.0f,
//                            .y = 0.0f,
//                            .width = static_cast<float>(extent.width),
//                            .height = static_cast<float>(extent.height),
//                            .minDepth = 0.0f,
//                            .maxDepth = 1.0f,
//                        }});
//     cmd.setScissor(0, {{
//                           .offset = {},
//                           .extent = extent,
//                       }});

//     cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *pp_pipeline_layout, 0,
//                            {*frame.pp_descriptor_set}, {});
//     cmd.draw(3, 1, 0, 0);
// }

VulkanRenderer::UniformBufferObject VulkanRenderer::GetUniformBufferObject() const {
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

glm::mat4 VulkanRenderer::GetPushConstant() const {
    static auto startTime = std::chrono::high_resolution_clock::now();

    const auto currentTime = std::chrono::high_resolution_clock::now();
    const float time =
        std::chrono::duration<float, std::chrono::seconds::period>(currentTime - startTime).count();

    auto proj = glm::perspective(
        glm::radians(45.0f),
        swap_chain->extent.width / static_cast<float>(swap_chain->extent.height), 0.1f, 10.0f);
    proj[1][1] *= -1;

    return proj *
           glm::lookAt(glm::vec3(2.0f, 2.0f, 2.0f), glm::vec3(0.0f, 0.0f, 0.0f),
                       glm::vec3(0.0f, 0.0f, 1.0f)) *
           glm::rotate(glm::mat4(1.0f), time * glm::radians(90.0f), glm::vec3(0.0f, 0.0f, 1.0f));
}

void VulkanRenderer::DrawFrame() {
    device->allocator->CleanupStagingBuffers();

    const auto& frame = pipeline->AcquireNextFrame();
    pipeline->WriteUniformObject<UniformBufferObject>({GetUniformBufferObject()});

    const auto& framebuffer = swap_chain->AcquireImage(*frame.render_start_semaphore);
    pipeline->BeginFrame({
        .framebuffer = *framebuffer->get(),
        .renderArea =
            {
                .extent = swap_chain->extent,
            },
    });
    frame.command_buffer.pushConstants<glm::mat4>(
        *pipeline->pipeline_layout, vk::ShaderStageFlagBits::eVertex, 0, {GetPushConstant()});

    // Dynamic states
    frame.command_buffer.setViewport(0, {{
                                            .x = 0.0f,
                                            .y = 0.0f,
                                            .width = static_cast<float>(swap_chain->extent.width),
                                            .height = static_cast<float>(swap_chain->extent.height),
                                            .minDepth = 0.0f,
                                            .maxDepth = 1.0f,
                                        }});
    frame.command_buffer.setScissor(0, {{
                                           .extent = swap_chain->extent,
                                       }});

    frame.command_buffer.drawIndexed(6, 1, 0, 0, 0);
    pipeline->EndFrame();

    device->graphics_queue.submit(
        {
            {
                .waitSemaphoreCount = 1,
                .pWaitSemaphores = TempArr<vk::Semaphore>{*frame.render_start_semaphore},
                .pWaitDstStageMask =
                    TempArr<vk::PipelineStageFlags>{
                        vk::PipelineStageFlagBits::eColorAttachmentOutput},
                .commandBufferCount = 1,
                .pCommandBuffers = TempArr<vk::CommandBuffer>{*frame.command_buffer},
                .signalSemaphoreCount = 1,
                .pSignalSemaphores = TempArr<vk::Semaphore>{*frame.render_finished_semaphore},
            },
        },
        *frame.in_flight_fence);
    swap_chain->Present(*frame.render_finished_semaphore);
}

void VulkanRenderer::RecreateSwapchain(const vk::Extent2D& actual_extent) {
    (*device)->waitIdle();

    swap_chain.reset(); // Need to destroy old first
    swap_chain = std::make_unique<VulkanSwapchain>(*device, actual_extent);
    swap_chain->CreateFramebuffers(pipeline->render_pass);
}

} // namespace Renderer
