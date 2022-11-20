// Copyright 2022 Pengfei Zhu
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <memory>
#include <utility>
#include <vulkan/vulkan_raii.hpp>

struct CommandBufferContext {
    explicit CommandBufferContext(vk::raii::CommandBuffer& command_buffer_,
                                  const vk::CommandBufferBeginInfo& begin_info)
        : command_buffer(command_buffer_) {

        command_buffer.begin(begin_info);
    }

    ~CommandBufferContext() {
        command_buffer.end();
    }

    vk::raii::CommandBuffer& command_buffer;
};

struct CommandBufferRenderPassContext {
    explicit CommandBufferRenderPassContext(vk::raii::CommandBuffer& command_buffer_,
                                            const vk::RenderPassBeginInfo& begin_info,
                                            vk::SubpassContents contents)
        : command_buffer(command_buffer_) {

        command_buffer.beginRenderPass(begin_info, contents);
    }

    ~CommandBufferRenderPassContext() {
        command_buffer.endRenderPass();
    }

    vk::raii::CommandBuffer& command_buffer;
};
