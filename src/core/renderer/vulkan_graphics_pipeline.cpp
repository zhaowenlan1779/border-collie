// Copyright 2022 Pengfei Zhu
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <map>
#include "common/ranges.h"
#include "common/temp_ptr.h"
#include "core/renderer/vulkan_device.h"
#include "core/renderer/vulkan_graphics_pipeline.h"

namespace Renderer {

VulkanGraphicsPipeline::VulkanGraphicsPipeline(const VulkanDevice& device,
                                               VulkanGraphicsPipelineCreateInfo create_info)
    : VulkanPipeline(device, create_info.descriptor_sets, create_info.push_constants) {

    render_pass = vk::raii::RenderPass{*device, create_info.render_pass_info};

    // Fill out the pipeline create info
    // TODO: This might be too restrictive?
    auto& pipeline_info = create_info.pipeline_info;

    static constexpr vk::PipelineVertexInputStateCreateInfo vertex_input_state{};
    if (!pipeline_info.pVertexInputState) {
        pipeline_info.pVertexInputState = &vertex_input_state;
    }

    static constexpr vk::PipelineInputAssemblyStateCreateInfo input_assembly_state{
        .topology = vk::PrimitiveTopology::eTriangleList,
        .primitiveRestartEnable = VK_FALSE,
    };
    if (!pipeline_info.pInputAssemblyState) {
        pipeline_info.pInputAssemblyState = &input_assembly_state;
    }

    std::vector<vk::DynamicState> dynamic_states;
    static constexpr vk::PipelineViewportStateCreateInfo viewport_state{
        .viewportCount = 1,
        .scissorCount = 1,
    };
    if (!pipeline_info.pViewportState) {
        pipeline_info.pViewportState = &viewport_state;
        dynamic_states.emplace_back(vk::DynamicState::eViewport);
        dynamic_states.emplace_back(vk::DynamicState::eScissor);
    }

    static constexpr vk::PipelineRasterizationStateCreateInfo rasterization_state{
        .cullMode = vk::CullModeFlagBits::eBack,
        .lineWidth = 1.0f,
    };
    if (!pipeline_info.pRasterizationState) {
        pipeline_info.pRasterizationState = &rasterization_state;
    }

    static constexpr vk::PipelineMultisampleStateCreateInfo multisample_state{};
    if (!pipeline_info.pMultisampleState) {
        pipeline_info.pMultisampleState = &multisample_state;
    }

    static constexpr vk::PipelineColorBlendAttachmentState color_blend_attachment{
        .colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
                          vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA,
    };
    static constexpr vk::PipelineColorBlendStateCreateInfo color_blend_state{
        .logicOpEnable = VK_FALSE,
        .attachmentCount = 1,
        .pAttachments = &color_blend_attachment,
    };
    if (!pipeline_info.pColorBlendState) {
        pipeline_info.pColorBlendState = &color_blend_state;
    }

    const vk::PipelineDynamicStateCreateInfo dynamic_state{
        .dynamicStateCount = static_cast<u32>(dynamic_states.size()),
        .pDynamicStates = dynamic_states.data(),
    };
    if (!pipeline_info.pDynamicState) {
        pipeline_info.pDynamicState = &dynamic_state;
    }

    pipeline_info.layout = *pipeline_layout;
    pipeline_info.renderPass = *render_pass;

    pipeline = vk::raii::Pipeline{*device, device.pipeline_cache, pipeline_info};

    const auto IsDynamicState = [&pipeline_info](vk::DynamicState state) {
        return std::find(pipeline_info.pDynamicState->pDynamicStates,
                         pipeline_info.pDynamicState->pDynamicStates +
                             pipeline_info.pDynamicState->dynamicStateCount,
                         state) != pipeline_info.pDynamicState->pDynamicStates +
                                       pipeline_info.pDynamicState->dynamicStateCount;
    };
    if (IsDynamicState(vk::DynamicState::eViewport) && IsDynamicState(vk::DynamicState::eScissor)) {
        dynamic_viewport_scissor = true;
    }
}

VulkanGraphicsPipeline::~VulkanGraphicsPipeline() = default;

void VulkanGraphicsPipeline::BeginFrame(vk::RenderPassBeginInfo render_pass_begin) {
    auto& frame = frames_in_flight[current_frame];

    VulkanPipeline::BeginFrame();
    const auto& command_buffer = frame.command_buffer;

    render_pass_begin.renderPass = *render_pass;
    static constexpr vk::ClearValue clear_value{{{{0.0f, 0.0f, 0.0f, 1.0f}}}};
    if (render_pass_begin.clearValueCount == 0) {
        render_pass_begin.clearValueCount = 1;
        render_pass_begin.pClearValues = &clear_value;
    }

    command_buffer.beginRenderPass(render_pass_begin, vk::SubpassContents::eInline);
    command_buffer.bindPipeline(vk::PipelineBindPoint::eGraphics, *pipeline);

    if (!vertex_buffers.empty()) {
        command_buffer.bindVertexBuffers(0, vertex_buffers,
                                         std::vector<vk::DeviceSize>(vertex_buffers.size(), 0));
    }
    if (index_buffer) {
        command_buffer.bindIndexBuffer(index_buffer, 0, index_buffer_type);
    }

    const auto& raw_descriptor_sets = Common::VectorFromRange(
        frame.descriptor_sets | std::views::transform(&vk::raii::DescriptorSet::operator*));
    command_buffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *pipeline_layout, 0,
                                      raw_descriptor_sets, {});

    if (dynamic_viewport_scissor) {
        command_buffer.setViewport(
            0, {{
                   .x = 0.0f,
                   .y = 0.0f,
                   .width = static_cast<float>(render_pass_begin.renderArea.extent.width),
                   .height = static_cast<float>(render_pass_begin.renderArea.extent.height),
                   .minDepth = 0.0f,
                   .maxDepth = 1.0f,
               }});
        command_buffer.setScissor(0, {{
                                         .extent = render_pass_begin.renderArea.extent,
                                     }});
    }
}

void VulkanGraphicsPipeline::EndFrame() {
    const auto& frame = frames_in_flight[current_frame];
    frame.command_buffer.endRenderPass();

    VulkanPipeline::EndFrame();
}

} // namespace Renderer
