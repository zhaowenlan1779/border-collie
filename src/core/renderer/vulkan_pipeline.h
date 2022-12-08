// Copyright 2022 Pengfei Zhu
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <array>
#include <memory>
#include <utility>
#include <vector>
#include <vulkan/vulkan_raii.hpp>
#include "common/common_types.h"
#include "core/renderer/vulkan_helpers.hpp"

namespace Renderer {

class VulkanBuffer;
class VulkanDevice;
class VulkanImage;
class VulkanUniformBuffer;

template <typename T>
inline vk::PushConstantRange PushConstant(vk::ShaderStageFlags stages) {
    static_assert(Helpers::VerifyLayoutStd140<T>());
    return {
        .stageFlags = stages,
        .offset = 0,
        .size = sizeof(T),
    };
}

struct DescriptorBinding {
    vk::DescriptorType type{};
    u32 count{};
    vk::ShaderStageFlags stages{};

    // Only one of the following 3 should be set. If `size` is set, `type` must be
    // eUniformBuffer, and the buffer will be created and owned by the pipeline.
    struct BufferRef {
        vk::ArrayProxy<const vk::Buffer> buffers;
    };
    vk::ArrayProxy<const BufferRef> buffers;

    struct ImageRef {
        vk::ArrayProxy<const vk::ImageView> images;
        vk::ImageLayout layout;
    };
    vk::ArrayProxy<const ImageRef> images;

    // Create uniform buffer
    std::size_t size{};
};

template <typename T>
inline DescriptorBinding UBO(vk::ShaderStageFlags stages) {
    static_assert(Helpers::VerifyLayoutStd140<T>());
    return {
        .type = vk::DescriptorType::eUniformBuffer,
        .count = 1,
        .stages = stages,
        .size = sizeof(T),
    };
}

using DescriptorSet = vk::ArrayProxy<const DescriptorBinding>;

struct FrameInFlight : NonCopyable {
    u32 index = 0;
    vk::raii::CommandBuffer command_buffer = nullptr;
    vk::raii::Semaphore render_start_semaphore = nullptr;
    vk::raii::Semaphore render_finished_semaphore = nullptr;
    vk::raii::Fence in_flight_fence = nullptr;
    std::vector<vk::raii::DescriptorSet> descriptor_sets;

    struct UniformBuffer {
        std::unique_ptr<VulkanUniformBuffer> buffer;
        vk::PipelineStageFlags2 dst_stages{};
    };
    std::vector<std::vector<UniformBuffer>> uniform_buffers{};
};

/**
 * Base class for a pipeline. Handles descriptors, push constants, frames in flight, etc.
 */
class VulkanPipeline : NonCopyable {
public:
    explicit VulkanPipeline(const VulkanDevice& device,
                            const vk::ArrayProxy<const DescriptorSet>& descriptor_sets,
                            const vk::ArrayProxy<const vk::PushConstantRange>& push_constants);

    void WriteUniformObject(const u8* data, std::size_t size, std::size_t idx,
                            std::size_t array_idx);

    template <typename T>
    void WriteUniformObject(const vk::ArrayProxy<const T>& objects, std::size_t idx = 0) {
        for (std::size_t i = 0; i < objects.size(); ++i) {
            WriteUniformObject(reinterpret_cast<const u8*>(objects.data() + i), sizeof(T), idx, i);
        }
    }

    FrameInFlight& AcquireNextFrame();
    void BeginFrame();
    void EndFrame();

    const VulkanDevice& device;
    vk::raii::DescriptorPool descriptor_pool = nullptr;
    std::vector<vk::raii::DescriptorSetLayout> descriptor_set_layouts;
    vk::raii::PipelineLayout pipeline_layout = nullptr;
    vk::raii::Pipeline pipeline = nullptr;

    vk::raii::Sampler sampler = nullptr;

    vk::raii::CommandPool command_pool = nullptr;
    std::array<FrameInFlight, 2> frames_in_flight;
    std::size_t current_frame = 0;

protected:
    ~VulkanPipeline();
};

} // namespace Renderer
