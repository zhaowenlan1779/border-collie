// Copyright 2022 Pengfei Zhu
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <algorithm>
#include <vector>
#include "core/vulkan/vulkan_device.h"
#include "core/vulkan/vulkan_graphics_pipeline.h"

namespace Renderer {

VulkanGraphicsPipeline::VulkanGraphicsPipeline(const VulkanDevice& device,
                                               vk::GraphicsPipelineCreateInfo pipeline_info,
                                               vk::raii::PipelineLayout pipeline_layout_)
    : pipeline_layout(std::move(pipeline_layout_)) {

    // Fill out the pipeline create info
    // TODO: This might be too restrictive?
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

    render_pass = pipeline_info.renderPass;
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

    pipeline = vk::raii::Pipeline{*device, device.pipeline_cache, pipeline_info};
}

VulkanGraphicsPipeline::~VulkanGraphicsPipeline() = default;

void VulkanGraphicsPipeline::BeginRenderPass(const vk::raii::CommandBuffer& command_buffer,
                                             vk::RenderPassBeginInfo render_pass_begin) const {
    render_pass_begin.renderPass = render_pass;
    static constexpr vk::ClearValue clear_value{{{{0.0f, 0.0f, 0.0f, 1.0f}}}};
    if (render_pass_begin.clearValueCount == 0) {
        render_pass_begin.clearValueCount = 1;
        render_pass_begin.pClearValues = &clear_value;
    }

    command_buffer.beginRenderPass(render_pass_begin, vk::SubpassContents::eInline);
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

void VulkanGraphicsPipeline::EndRenderPass(const vk::raii::CommandBuffer& command_buffer) const {
    command_buffer.endRenderPass();
}

} // namespace Renderer
