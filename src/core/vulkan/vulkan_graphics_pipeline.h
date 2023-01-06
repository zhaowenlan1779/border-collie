// Copyright 2022 Pengfei Zhu
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <memory>
#include <utility>
#include <vulkan/vulkan_raii.hpp>
#include "common/common_types.h"

namespace Renderer {

class VulkanDevice;

class VulkanGraphicsPipeline : NonCopyable {
public:
    // The parameters in GraphicsPipelineCreateInfo will be set to default values if not set.
    explicit VulkanGraphicsPipeline(const VulkanDevice& device,
                                    vk::GraphicsPipelineCreateInfo pipeline_info,
                                    vk::raii::PipelineLayout pipeline_layout);
    ~VulkanGraphicsPipeline();

    vk::Pipeline operator*() const noexcept {
        return *pipeline;
    }

    void BeginRenderPass(const vk::raii::CommandBuffer& command_buffer,
                         vk::RenderPassBeginInfo render_pass_begin) const;
    void EndRenderPass(const vk::raii::CommandBuffer& command_buffer) const;

    vk::raii::Pipeline pipeline = nullptr;
    vk::raii::PipelineLayout pipeline_layout = nullptr;
    vk::RenderPass render_pass{};
    bool dynamic_viewport_scissor = false;
};

} // namespace Renderer
