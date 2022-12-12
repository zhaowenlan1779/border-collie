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

struct Vertex {
    glm::vec2 pos;
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

    // Pipeline
    render_pass = vk::raii::RenderPass{
        **device,
        {
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
            .renderPass = *render_pass,
        },
        frames->CreatePipelineLayout({
            PushConstant<glm::mat4>(vk::ShaderStageFlagBits::eVertex),
        }));

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
    const auto& cmd = frame.command_buffer;
    frame.extras.uniform->Update(GetUniformBufferObject());

    frames->BeginFrame();
    frame.extras.uniform->Upload(cmd);

    pipeline->BeginRenderPass(cmd, {
                                       .framebuffer = *frame.extras.framebuffer,
                                       .renderArea =
                                           {
                                               .extent = swap_chain->extent,
                                           },
                                   });
    cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, **pipeline);
    cmd.bindVertexBuffers(0, {**vertex_buffer}, {0});
    cmd.bindIndexBuffer(**index_buffer, 0, vk::IndexType::eUint16);
    cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *pipeline->pipeline_layout, 0,
                           frames->GetDescriptorSets(), {});
    cmd.pushConstants<glm::mat4>(*pipeline->pipeline_layout, vk::ShaderStageFlagBits::eVertex, 0,
                                 {GetPushConstant()});
    cmd.drawIndexed(6, 1, 0, 0, 0);
    cmd.endRenderPass();

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
                .attachmentCount = 1,
                .pAttachments =
                    TempArr<vk::ImageView>{*pp_frames->frames_in_flight[i].extras.image_view},
                .width = swap_chain->extent.width,
                .height = swap_chain->extent.height,
                .layers = 1,
            }};
    }
}

void VulkanRasterizer::OnResized([[maybe_unused]] const vk::Extent2D& actual_extent) {
    VulkanRenderer::OnResized(actual_extent);
    CreateFramebuffers();
}

} // namespace Renderer
