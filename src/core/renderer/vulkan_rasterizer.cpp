// Copyright 2022 Pengfei Zhu
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <chrono>
#include <glm/gtc/matrix_transform.hpp>
#include "core/renderer/vulkan_buffer.h"
#include "core/renderer/vulkan_graphics_pipeline.h"
#include "core/renderer/vulkan_helpers.hpp"
#include "core/renderer/vulkan_rasterizer.h"
#include "core/renderer/vulkan_renderer.h"
#include "core/renderer/vulkan_shader.h"
#include "core/renderer/vulkan_swapchain.h"
#include "core/renderer/vulkan_texture.h"

namespace Renderer {

struct Vertex {
    glm::vec2 pos;
    glm::vec3 color;
    glm::vec2 texCoord;
};

VulkanRasterizer::VulkanRasterizer(const VulkanRenderer& renderer_) : renderer(renderer_) {
    // Buffers & Textures
    const std::vector<Vertex> vertices = {{{-0.5f, -0.5f}, {1.0f, 0.0f, 0.0f}, {1.0f, 0.0f}},
                                          {{0.5f, -0.5f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f}},
                                          {{0.5f, 0.5f}, {0.0f, 0.0f, 1.0f}, {0.0f, 1.0f}},
                                          {{-0.5f, 0.5f}, {1.0f, 1.0f, 1.0f}, {1.0f, 1.0f}}};
    vertex_buffer = std::make_unique<VulkanImmUploadBuffer>(
        *renderer.device, VulkanImmUploadBufferCreateInfo{
                              .data = reinterpret_cast<const u8*>(vertices.data()),
                              .size = vertices.size() * sizeof(vertices[0]),
                              .usage = vk::BufferUsageFlagBits::eVertexBuffer,
                              .dst_stage_mask = vk::PipelineStageFlagBits2::eVertexAttributeInput,
                              .dst_access_mask = vk::AccessFlagBits2::eVertexAttributeRead,
                          });

    const std::vector<u16> indices = {0, 1, 2, 2, 3, 0};
    index_buffer = std::make_unique<VulkanImmUploadBuffer>(
        *renderer.device, VulkanImmUploadBufferCreateInfo{
                              .data = reinterpret_cast<const u8*>(indices.data()),
                              .size = indices.size() * sizeof(indices[0]),
                              .usage = vk::BufferUsageFlagBits::eIndexBuffer,
                              .dst_stage_mask = vk::PipelineStageFlagBits2::eIndexInput,
                              .dst_access_mask = vk::AccessFlagBits2::eIndexRead,
                          });

    texture = std::make_unique<VulkanTexture>(*renderer.device, u8"textures/texture.jpg");

    // Pipeline
    static constexpr auto VertexAttributeDescriptions = Helpers::AttributeDescriptionsFor<Vertex>();

    // clang-format off
    pipeline = std::make_unique<VulkanGraphicsPipeline>(*renderer.device, VulkanGraphicsPipelineCreateInfo{
        .render_pass_info = {
            .attachmentCount = 1,
            .pAttachments = TempArr<vk::AttachmentDescription>{{
                .format = renderer.swap_chain->surface_format.format,
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
                    .module = *VulkanShader{**renderer.device, u8"core/renderer/shaders/test.vert"},
                    .pName = "main",
                },
                {
                    .stage = vk::ShaderStageFlagBits::eFragment,
                    .module = *VulkanShader{**renderer.device, u8"core/renderer/shaders/test.frag"},
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
    // clang-format on

    // Assign buffers
    pipeline->vertex_buffers = {**vertex_buffer};
    pipeline->index_buffer = **index_buffer;
    pipeline->index_buffer_type = vk::IndexType::eUint16;

    CreateFramebuffers();
}

VulkanRasterizer::~VulkanRasterizer() = default;

struct VulkanRasterizer::UniformBufferObject {
    glm::mat4 model;
    glm::mat4 view;
    glm::mat4 proj;
};

VulkanRasterizer::UniformBufferObject VulkanRasterizer::GetUniformBufferObject() const {
    static auto startTime = std::chrono::high_resolution_clock::now();

    const auto currentTime = std::chrono::high_resolution_clock::now();
    const float time =
        std::chrono::duration<float, std::chrono::seconds::period>(currentTime - startTime).count();

    auto proj = glm::perspective(glm::radians(45.0f),
                                 renderer.swap_chain->extent.width /
                                     static_cast<float>(renderer.swap_chain->extent.height),
                                 0.1f, 10.0f);
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

    auto proj = glm::perspective(glm::radians(45.0f),
                                 renderer.swap_chain->extent.width /
                                     static_cast<float>(renderer.swap_chain->extent.height),
                                 0.1f, 10.0f);
    proj[1][1] *= -1;

    return proj *
           glm::lookAt(glm::vec3(2.0f, 2.0f, 2.0f), glm::vec3(0.0f, 0.0f, 0.0f),
                       glm::vec3(0.0f, 0.0f, 1.0f)) *
           glm::rotate(glm::mat4(1.0f), time * glm::radians(90.0f), glm::vec3(0.0f, 0.0f, 1.0f));
}

const FrameInFlight& VulkanRasterizer::DrawFrame() {
    const auto& frame = pipeline->AcquireNextFrame();
    pipeline->WriteUniformObject<UniformBufferObject>({GetUniformBufferObject()});

    pipeline->BeginFrame({
        .framebuffer = *framebuffers[frame.index],
        .renderArea =
            {
                .extent = renderer.swap_chain->extent,
            },
    });
    frame.command_buffer.pushConstants<glm::mat4>(
        *pipeline->pipeline_layout, vk::ShaderStageFlagBits::eVertex, 0, {GetPushConstant()});
    frame.command_buffer.drawIndexed(6, 1, 0, 0, 0);
    pipeline->EndFrame();

    renderer.device->graphics_queue.submit(
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

void VulkanRasterizer::CreateFramebuffers() {
    for (std::size_t i = 0; i < framebuffers.size(); ++i) {
        framebuffers[i] = vk::raii::Framebuffer{
            **renderer.device,
            vk::FramebufferCreateInfo{
                .renderPass = *pipeline->render_pass,
                .attachmentCount = 1,
                .pAttachments = TempArr<vk::ImageView>{*renderer.offscreen_frames[i].image_view},
                .width = renderer.swap_chain->extent.width,
                .height = renderer.swap_chain->extent.height,
                .layers = 1,
            }};
    }
}

void VulkanRasterizer::OnResized([[maybe_unused]] const vk::Extent2D& actual_extent) {
    CreateFramebuffers();
}

} // namespace Renderer
