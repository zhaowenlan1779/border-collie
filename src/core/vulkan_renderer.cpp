// Copyright 2022 Pengfei Zhu
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <array>
#include <spdlog/spdlog.h>
#include "common/temp_ptr.h"
#include "core/vulkan/vulkan_allocator.h"
#include "core/vulkan/vulkan_context.h"
#include "core/vulkan/vulkan_device.h"
#include "core/vulkan/vulkan_frames_in_flight.hpp"
#include "core/vulkan/vulkan_graphics_pipeline.h"
#include "core/vulkan/vulkan_helpers.hpp"
#include "core/vulkan/vulkan_shader.h"
#include "core/vulkan/vulkan_swapchain.h"
#include "core/vulkan/vulkan_texture.h"
#include "core/vulkan_renderer.h"

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

void VulkanRenderer::Init(vk::SurfaceKHR surface, const vk::Extent2D& actual_extent) {
    device = CreateDevice(surface, actual_extent);
    swap_chain = std::make_unique<VulkanSwapchain>(*device, actual_extent);

    pp_frames = std::make_unique<VulkanFramesInFlight<OffscreenFrame, 2>>(*device);

    CreateRenderTargets();
    pp_frames->CreateDescriptors({{
        {
            .type = vk::DescriptorType::eCombinedImageSampler,
            .count = 1,
            .stages = vk::ShaderStageFlagBits::eFragment,
            .images =
                {
                    {
                        .images =
                            {
                                *pp_frames->frames_in_flight[0].extras.image_view,
                                *pp_frames->frames_in_flight[1].extras.image_view,
                            },
                        .layout = vk::ImageLayout::eGeneral,
                    },
                },
        },
    }});

    pp_render_pass = vk::raii::RenderPass{
        **device,
        {
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
        }};

    pp_pipeline = std::make_unique<VulkanGraphicsPipeline>(
        *device,
        vk::GraphicsPipelineCreateInfo{
            .stageCount = 2,
            .pStages = TempArr<vk::PipelineShaderStageCreateInfo>{{
                {
                    .stage = vk::ShaderStageFlagBits::eVertex,
                    .module = *VulkanShader{**device, u8"core/shaders/postprocessing.vert"},
                    .pName = "main",
                },
                {
                    .stage = vk::ShaderStageFlagBits::eFragment,
                    .module = *VulkanShader{**device, u8"core/shaders/postprocessing.frag"},
                    .pName = "main",
                },
            }},
            .pRasterizationState = TempPtr{vk::PipelineRasterizationStateCreateInfo{
                .cullMode = vk::CullModeFlagBits::eNone,
                .lineWidth = 1.0f,
            }},
            .renderPass = *pp_render_pass,
        },
        pp_frames->CreatePipelineLayout({
            PushConstant<glm::mat4>(vk::ShaderStageFlagBits::eVertex),
        }));

    swap_chain->CreateFramebuffers(pp_render_pass);
}

void VulkanRenderer::CreateRenderTargets() {
    Helpers::OneTimeCommandContext context{*device};

    const auto& info = GetOffscreenImageInfo();
    for (auto& frame_in_flight : pp_frames->frames_in_flight) {
        auto& frame = frame_in_flight.extras;

        frame.render_start_semaphore = vk::raii::Semaphore{**device, vk::SemaphoreCreateInfo{}};

        frame.image = std::make_unique<VulkanImage>(
            *device->allocator,
            vk::ImageCreateInfo{
                .imageType = vk::ImageType::e2D,
                .format = info.format,
                .extent =
                    {
                        .width = swap_chain->extent.width,
                        .height = swap_chain->extent.height,
                        .depth = 1,
                    },
                .mipLevels = 1,
                .arrayLayers = 1,
                .usage = info.usage | vk::ImageUsageFlagBits::eSampled,
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
                .dstStageMask = info.dst_stage_mask | vk::PipelineStageFlagBits2::eFragmentShader,
                .dstAccessMask = info.dst_access_mask | vk::AccessFlagBits2::eUniformRead,
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
                                    .format = info.format,
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

void VulkanRenderer::PostprocessAndPresent(vk::Semaphore offscreen_render_finished_semaphore) {
    const auto& frame = pp_frames->AcquireNextFrame();
    const auto& cmd = frame.command_buffer;

    const auto& framebuffer = swap_chain->AcquireImage(*frame.extras.render_start_semaphore);
    if (!framebuffer.has_value()) {
        SPDLOG_ERROR("Failed to acquire image, ignoring");
        return;
    }

    pp_frames->BeginFrame();

    pp_pipeline->BeginRenderPass(cmd, {
                                          .framebuffer = *framebuffer->get(),
                                          .renderArea =
                                              {
                                                  .extent = swap_chain->extent,
                                              },
                                      });
    cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, **pp_pipeline);
    cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *pp_pipeline->pipeline_layout, 0,
                           pp_frames->GetDescriptorSets(), {});
    cmd.draw(3, 1, 0, 0);
    cmd.endRenderPass();

    pp_frames->EndFrame();

    device->graphics_queue.submit(
        {
            {
                .waitSemaphoreCount = 2,
                .pWaitSemaphores = TempArr<vk::Semaphore>{offscreen_render_finished_semaphore,
                                                          *frame.extras.render_start_semaphore},
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

void VulkanRenderer::OnResized(const vk::Extent2D& actual_extent) {
    (*device)->waitIdle();

    swap_chain.reset(); // Need to destroy old first
    swap_chain = std::make_unique<VulkanSwapchain>(*device, actual_extent);
    swap_chain->CreateFramebuffers(pp_render_pass);

    CreateRenderTargets();
    pp_frames->UpdateDescriptor(0, 0,
                                {
                                    .type = vk::DescriptorType::eCombinedImageSampler,
                                    .count = 1,
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
