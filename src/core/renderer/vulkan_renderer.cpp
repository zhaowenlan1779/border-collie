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
    CreateRenderTargets();

    // Pipeline
    static constexpr auto VertexAttributeDescriptions = Helpers::AttributeDescriptionsFor<Vertex>();

    // clang-format off
    pipeline = std::make_unique<VulkanGraphicsPipeline>(*device, VulkanGraphicsPipelineCreateInfo{
        .render_pass_info = {
            .attachmentCount = 1,
            .pAttachments = TempArr<vk::AttachmentDescription>{{
                .format = swap_chain->surface_format.format,
                .loadOp = vk::AttachmentLoadOp::eClear,
                .storeOp = vk::AttachmentStoreOp::eStore,
                .stencilLoadOp = vk::AttachmentLoadOp::eDontCare,
                .stencilStoreOp = vk::AttachmentStoreOp::eDontCare,
                .finalLayout = vk::ImageLayout::eGeneral,
            }},
            .subpassCount = 1,
            .pSubpasses = TempArr<vk::SubpassDescription>{{
                .pipelineBindPoint = vk::PipelineBindPoint::eGraphics,
                .colorAttachmentCount = 1,
                .pColorAttachments = TempArr<vk::AttachmentReference>{{
                    .attachment = 0,
                    .layout = vk::ImageLayout::eGeneral,
                }},
            }},
            .dependencyCount = 1,
            .pDependencies = TempArr<vk::SubpassDependency>{{
                .srcSubpass = VK_SUBPASS_EXTERNAL,
                .dstSubpass = 0,
                .srcStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput,
                .dstStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput,
                .srcAccessMask = vk::AccessFlags{},
                .dstAccessMask = vk::AccessFlagBits::eColorAttachmentWrite,
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
                        .images = {*texture->image_view},
                        .layout = vk::ImageLayout::eShaderReadOnlyOptimal,
                    },
                },
            },
        }},
        .push_constants = {
            PushConstant<glm::mat4>(vk::ShaderStageFlagBits::eVertex),
        },
    });

    pp_pipeline = std::make_unique<VulkanGraphicsPipeline>(*device, VulkanGraphicsPipelineCreateInfo{
        .render_pass_info = {
            .attachmentCount = 1,
            .pAttachments = TempArr<vk::AttachmentDescription>{{
                .format = swap_chain->surface_format.format,
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
                    .module = *VulkanShader{**device, u8"core/renderer/shaders/postprocessing.vert"},
                    .pName = "main",
                },
                {
                    .stage = vk::ShaderStageFlagBits::eFragment,
                    .module = *VulkanShader{**device, u8"core/renderer/shaders/postprocessing.frag"},
                    .pName = "main",
                },
            }},
            .pRasterizationState = TempPtr{vk::PipelineRasterizationStateCreateInfo{
                .cullMode = vk::CullModeFlagBits::eNone,
                .lineWidth = 1.0f,
            }},
        },
        .descriptor_sets = {{
            {
                .type = vk::DescriptorType::eCombinedImageSampler,
                .count = 1,
                .stages = vk::ShaderStageFlagBits::eFragment,
                .images = {
                    {
                        .images = {
                            *offscreen_frames[0].image_view,
                            *offscreen_frames[1].image_view,
                        },
                        .layout = vk::ImageLayout::eGeneral,
                    },
                },
            },
        }},
    });
    // clang-format on

    swap_chain->CreateFramebuffers(pp_pipeline->render_pass);
    CreateRenderTargetFramebuffers();

    // Assign buffers
    pipeline->vertex_buffers = {**vertex_buffer};
    pipeline->index_buffer = **index_buffer;
    pipeline->index_buffer_type = vk::IndexType::eUint16;
}

void VulkanRenderer::CreateRenderTargets() {
    Helpers::OneTimeCommandContext context{*device};

    for (auto& frame : offscreen_frames) {
        frame.image =
            std::make_unique<VulkanImage>(*device->allocator,
                                          vk::ImageCreateInfo{
                                              .imageType = vk::ImageType::e2D,
                                              .format = swap_chain->surface_format.format,
                                              .extent =
                                                  {
                                                      .width = swap_chain->extent.width,
                                                      .height = swap_chain->extent.height,
                                                      .depth = 1,
                                                  },
                                              .mipLevels = 1,
                                              .arrayLayers = 1,
                                              .usage = vk::ImageUsageFlagBits::eColorAttachment |
                                                       vk::ImageUsageFlagBits::eSampled,
                                              .initialLayout = vk::ImageLayout::eUndefined,
                                          },
                                          VmaAllocationCreateInfo{
                                              .flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT,
                                              .usage = VMA_MEMORY_USAGE_AUTO,
                                              .priority = 1.0f,
                                          });

        Helpers::ImageLayoutTransition(
            *context, frame.image,
            {
                .srcStageMask = vk::PipelineStageFlagBits2::eTopOfPipe,
                .srcAccessMask = vk::AccessFlags2{},
                .dstStageMask = vk::PipelineStageFlagBits2::eColorAttachmentOutput |
                                vk::PipelineStageFlagBits2::eFragmentShader,
                .dstAccessMask =
                    vk::AccessFlagBits2::eColorAttachmentWrite | vk::AccessFlagBits2::eUniformRead,
                .oldLayout = vk::ImageLayout::eUndefined,
                .newLayout = vk::ImageLayout::eGeneral,
                .subresourceRange =
                    {
                        .baseMipLevel = 0,
                        .levelCount = 1,
                    },
            });

        frame.image_view =
            vk::raii::ImageView{**device,
                                {
                                    .image = **frame.image,
                                    .viewType = vk::ImageViewType::e2D,
                                    .format = swap_chain->surface_format.format,
                                    .subresourceRange =
                                        {
                                            .aspectMask = vk::ImageAspectFlagBits::eColor,
                                            .baseMipLevel = 0,
                                            .levelCount = 1,
                                            .baseArrayLayer = 0,
                                            .layerCount = 1,
                                        },
                                }};
    }
}

void VulkanRenderer::CreateRenderTargetFramebuffers() {
    for (auto& frame : offscreen_frames) {
        frame.framebuffer = vk::raii::Framebuffer{
            **device, vk::FramebufferCreateInfo{
                          .renderPass = *pipeline->render_pass,
                          .attachmentCount = 1,
                          .pAttachments = TempArr<vk::ImageView>{*frame.image_view},
                          .width = swap_chain->extent.width,
                          .height = swap_chain->extent.height,
                          .layers = 1,
                      }};
    }
}

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

const FrameInFlight& VulkanRenderer::DrawFrameOffscreen() {
    const auto& frame = pipeline->AcquireNextFrame();
    pipeline->WriteUniformObject<UniformBufferObject>({GetUniformBufferObject()});

    pipeline->BeginFrame({
        .framebuffer = *offscreen_frames[frame.index].framebuffer,
        .renderArea =
            {
                .extent = swap_chain->extent,
            },
    });
    frame.command_buffer.pushConstants<glm::mat4>(
        *pipeline->pipeline_layout, vk::ShaderStageFlagBits::eVertex, 0, {GetPushConstant()});
    frame.command_buffer.drawIndexed(6, 1, 0, 0, 0);
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

void VulkanRenderer::PostprocessAndPresent(const FrameInFlight& offscreen_frame) {
    const auto& frame = pp_pipeline->AcquireNextFrame();

    const auto& framebuffer = swap_chain->AcquireImage(*frame.render_start_semaphore);
    if (!framebuffer.has_value()) {
        SPDLOG_ERROR("Failed to acquire image, ignoring");
        return;
    }

    pp_pipeline->BeginFrame({
        .framebuffer = *framebuffer->get(),
        .renderArea =
            {
                .extent = swap_chain->extent,
            },
    });
    frame.command_buffer.draw(3, 1, 0, 0);
    pp_pipeline->EndFrame();

    device->graphics_queue.submit(
        {
            {
                .waitSemaphoreCount = 2,
                .pWaitSemaphores =
                    TempArr<vk::Semaphore>{*offscreen_frame.render_finished_semaphore,
                                           *frame.render_start_semaphore},
                .pWaitDstStageMask =
                    TempArr<vk::PipelineStageFlags>{
                        vk::PipelineStageFlagBits::eFragmentShader,
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

void VulkanRenderer::DrawFrame() {
    device->allocator->CleanupStagingBuffers();

    const auto& offscreen_frame = DrawFrameOffscreen();
    PostprocessAndPresent(offscreen_frame);
}

void VulkanRenderer::RecreateSwapchain(const vk::Extent2D& actual_extent) {
    (*device)->waitIdle();

    swap_chain.reset(); // Need to destroy old first
    swap_chain = std::make_unique<VulkanSwapchain>(*device, actual_extent);
    swap_chain->CreateFramebuffers(pp_pipeline->render_pass);

    CreateRenderTargets();
    CreateRenderTargetFramebuffers();
    pp_pipeline->UpdateDescriptor(0, 0,
                                  {
                                      .type = vk::DescriptorType::eCombinedImageSampler,
                                      .count = 1,
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
