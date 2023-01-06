// Copyright 2022 Pengfei Zhu
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <array>
#include <chrono>
#include <glm/gtc/matrix_transform.hpp>
#include "core/vulkan/vulkan_allocator.h"
#include "core/vulkan/vulkan_buffer.h"
#include "core/vulkan/vulkan_context.h"
#include "core/vulkan/vulkan_frames_in_flight.hpp"
#include "core/vulkan/vulkan_graphics_pipeline.h"
#include "core/vulkan/vulkan_helpers.hpp"
#include "core/vulkan/vulkan_shader.h"
#include "core/vulkan/vulkan_swapchain.h"
#include "core/vulkan/vulkan_texture.h"
#include "core/vulkan_rasterizer.h"

#include <filesystem>
#include <fstream>
#include <spdlog/spdlog.h>

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

struct Vertex {
    glm::vec3 pos;
    glm::vec3 color;
    glm::vec2 texCoord;
};

struct VulkanRasterizer::UniformBufferObject {
    glm::mat4 model;
    glm::mat4 view;
    glm::mat4 proj;
};

void VulkanRasterizer::Init(vk::SurfaceKHR surface, const vk::Extent2D& actual_extent) {
    VulkanRenderer::Init(surface, actual_extent);

    // Buffers & Textures
    const std::vector<Vertex> vertices = {{{-0.5f, -0.5f, 0.0f}, {1.0f, 0.0f, 0.0f}, {0.0f, 0.0f}},
                                          {{0.5f, -0.5f, 0.0f}, {0.0f, 1.0f, 0.0f}, {1.0f, 0.0f}},
                                          {{0.5f, 0.5f, 0.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 1.0f}},
                                          {{-0.5f, 0.5f, 0.0f}, {1.0f, 1.0f, 1.0f}, {0.0f, 1.0f}},

                                          {{-0.5f, -0.5f, -0.5f}, {1.0f, 0.0f, 0.0f}, {0.0f, 0.0f}},
                                          {{0.5f, -0.5f, -0.5f}, {0.0f, 1.0f, 0.0f}, {1.0f, 0.0f}},
                                          {{0.5f, 0.5f, -0.5f}, {0.0f, 0.0f, 1.0f}, {1.0f, 1.0f}},
                                          {{-0.5f, 0.5f, -0.5f}, {1.0f, 1.0f, 1.0f}, {0.0f, 1.0f}}};
    vertex_buffer = std::make_unique<VulkanImmUploadBuffer>(
        *device, VulkanImmUploadBufferCreateInfo{
                     .data = reinterpret_cast<const u8*>(vertices.data()),
                     .size = vertices.size() * sizeof(vertices[0]),
                     .usage = vk::BufferUsageFlagBits::eVertexBuffer,
                     .dst_stage_mask = vk::PipelineStageFlagBits2::eVertexAttributeInput,
                     .dst_access_mask = vk::AccessFlagBits2::eVertexAttributeRead,
                 });

    const std::vector<u16> indices = {0, 1, 2, 2, 3, 0, 4, 5, 6, 6, 7, 4};
    index_buffer = std::make_unique<VulkanImmUploadBuffer>(
        *device, VulkanImmUploadBufferCreateInfo{
                     .data = reinterpret_cast<const u8*>(indices.data()),
                     .size = indices.size() * sizeof(indices[0]),
                     .usage = vk::BufferUsageFlagBits::eIndexBuffer,
                     .dst_stage_mask = vk::PipelineStageFlagBits2::eIndexInput,
                     .dst_access_mask = vk::AccessFlagBits2::eIndexRead,
                 });

    texture = std::make_unique<VulkanTexture>(*device, u8"textures/texture.jpg");

    frames = std::make_unique<VulkanFramesInFlight<Frame, 2>>(*device);
    for (auto& frame_in_flight : frames->frames_in_flight) {
        frame_in_flight.extras.uniform =
            std::make_unique<VulkanUniformBufferObject<UniformBufferObject>>(
                *device->allocator, vk::PipelineStageFlagBits2::eVertexShader);
    }
    frames->CreateDescriptors({{
        {
            .type = vk::DescriptorType::eUniformBuffer,
            .count = 1,
            .stages = vk::ShaderStageFlagBits::eVertex,
            .buffers = {{
                {
                    *frames->frames_in_flight[0].extras.uniform->dst_buffer,
                    *frames->frames_in_flight[1].extras.uniform->dst_buffer,
                },
            }},
        },
        {
            .type = vk::DescriptorType::eCombinedImageSampler,
            .count = 1,
            .stages = vk::ShaderStageFlagBits::eFragment,
            .images =
                {
                    {
                        .images = {*texture->image_view},
                        .layout = vk::ImageLayout::eShaderReadOnlyOptimal,
                    },
                },
        },
    }});

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

    static constexpr auto VertexAttributeDescriptions = Helpers::AttributeDescriptionsFor<Vertex>();
    pipeline = std::make_unique<VulkanGraphicsPipeline>(
        *device,
        vk::GraphicsPipelineCreateInfo{
            .stageCount = 2,
            .pStages = TempArr<vk::PipelineShaderStageCreateInfo>{{
                {
                    .stage = vk::ShaderStageFlagBits::eVertex,
                    .module = *VulkanShader{**device, u8"core/shaders/test.vert"},
                    .pName = "main",
                },
                {
                    .stage = vk::ShaderStageFlagBits::eFragment,
                    .module = *VulkanShader{**device, u8"core/shaders/test.frag"},
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
            .pDepthStencilState = TempPtr{vk::PipelineDepthStencilStateCreateInfo{
                .depthTestEnable = VK_TRUE,
                .depthWriteEnable = VK_TRUE,
                .depthCompareOp = vk::CompareOp::eLess,
            }},
            .renderPass = *render_pass,
        },
        frames->CreatePipelineLayout({
            PushConstant<glm::mat4>(vk::ShaderStageFlagBits::eVertex),
        }));

    CreateDepthResources();
    CreateFramebuffers();
}

VulkanRasterizer::UniformBufferObject VulkanRasterizer::GetUniformBufferObject() const {
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

glm::mat4 VulkanRasterizer::GetPushConstant() const {
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

void VulkanRasterizer::DrawFrame() {
    device->allocator->CleanupStagingBuffers();

    const auto& frame = frames->AcquireNextFrame();
    frame.extras.uniform->Update(GetUniformBufferObject());

    frames->BeginFrame();

    const auto& cmd = frame.command_buffer;
    frame.extras.uniform->Upload(cmd);

    pipeline->BeginRenderPass(cmd, {
                                       .framebuffer = *frame.extras.framebuffer,
                                       .renderArea =
                                           {
                                               .extent = swap_chain->extent,
                                           },
                                       .clearValueCount = 2,
                                       .pClearValues =
                                           TempArr<vk::ClearValue>{
                                               {.color = {{{0.0f, 0.0f, 0.0f, 0.0f}}}},
                                               {.depthStencil = {1.0f, 0}},
                                           },
                                   });
    cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, **pipeline);
    cmd.bindVertexBuffers(0, {**vertex_buffer}, {0});
    cmd.bindIndexBuffer(**index_buffer, 0, vk::IndexType::eUint16);
    cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *pipeline->pipeline_layout, 0,
                           frames->GetDescriptorSets(), {});
    cmd.pushConstants<glm::mat4>(*pipeline->pipeline_layout, vk::ShaderStageFlagBits::eVertex, 0,
                                 {GetPushConstant()});
    cmd.drawIndexed(12, 1, 0, 0, 0);
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
