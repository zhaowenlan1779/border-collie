// Copyright 2022 Pengfei Zhu
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <vector>
#include <vulkan/vulkan_raii.hpp>
#include "core/renderer/vulkan_pipeline.h"

namespace Renderer {

struct VulkanGraphicsPipelineCreateInfo {
    vk::RenderPassCreateInfo render_pass_info;
    vk::GraphicsPipelineCreateInfo pipeline_info;
    vk::ArrayProxy<const DescriptorSet> descriptor_sets;
    vk::ArrayProxy<const vk::PushConstantRange> push_constants;
};
class VulkanGraphicsPipeline final : public VulkanPipeline {
public:
    // The parameters in GraphicsPipelineCreateInfo will be set to default values if not set.
    explicit VulkanGraphicsPipeline(const VulkanDevice& device,
                                    VulkanGraphicsPipelineCreateInfo create_info);
    ~VulkanGraphicsPipeline();

    // No need to actually specify the render_pass here
    void BeginFrame(vk::RenderPassBeginInfo render_pass_begin);
    void EndFrame();

    vk::raii::RenderPass render_pass = nullptr;
    vk::raii::Pipeline pipeline = nullptr;

    std::vector<vk::Buffer> vertex_buffers;
    vk::Buffer index_buffer{};
    vk::IndexType index_buffer_type = vk::IndexType::eUint32;
    bool dynamic_viewport_scissor = false;
};

} // namespace Renderer
