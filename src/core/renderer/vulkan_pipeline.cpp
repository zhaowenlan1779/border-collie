// Copyright 2022 Pengfei Zhu
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <map>
#include "common/ranges.h"
#include "common/temp_ptr.h"
#include "core/renderer/vulkan_buffer.h"
#include "core/renderer/vulkan_device.h"
#include "core/renderer/vulkan_pipeline.h"

namespace Renderer {

static vk::PipelineStageFlags2 ToPipelineStages(vk::ShaderStageFlags shader_stages) {
    vk::PipelineStageFlags2 pipeline_stages{};
    if (shader_stages & vk::ShaderStageFlagBits::eVertex) {
        pipeline_stages |= vk::PipelineStageFlagBits2::eVertexShader;
    }
    if (shader_stages & vk::ShaderStageFlagBits::eFragment) {
        pipeline_stages |= vk::PipelineStageFlagBits2::eFragmentShader;
    }
    return pipeline_stages;
}

VulkanPipeline::VulkanPipeline(const VulkanDevice& device_,
                               const vk::ArrayProxy<const DescriptorSet>& descriptor_sets,
                               const vk::ArrayProxy<const vk::PushConstantRange>& push_constants)
    : device(device_) {

    for (std::size_t i = 0; i < frames_in_flight.size(); ++i) {
        frames_in_flight[i].index = i;
    }

    // Sampler
    sampler = vk::raii::Sampler{
        *device,
        {
            .magFilter = vk::Filter::eLinear,
            .minFilter = vk::Filter::eLinear,
            .mipmapMode = vk::SamplerMipmapMode::eLinear,
            .addressModeU = vk::SamplerAddressMode::eRepeat,
            .addressModeV = vk::SamplerAddressMode::eRepeat,
            .addressModeW = vk::SamplerAddressMode::eRepeat,
            .mipLodBias = 0.0f,
            .anisotropyEnable = VK_TRUE,
            .maxAnisotropy = device.physical_device.getProperties().limits.maxSamplerAnisotropy,
            .minLod = 0.0f,
            .maxLod = 0.0f,
            .borderColor = vk::BorderColor::eIntOpaqueBlack,
        }};

    // Create descriptors
    // Determine the required numbers of the descriptors
    std::map<vk::DescriptorType, u32> descriptor_type_count;
    for (const auto& set : descriptor_sets) {
        for (const auto& binding : set) {
            descriptor_type_count[binding.type] += binding.count * frames_in_flight.size();
        }
    }
    descriptor_pool = vk::raii::DescriptorPool{
        *device,
        {
            .flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet,
            .maxSets = static_cast<u32>(descriptor_sets.size() * frames_in_flight.size()),
            .poolSizeCount = static_cast<u32>(descriptor_type_count.size()),
            .pPoolSizes = Common::VectorFromRange(descriptor_type_count |
                                                  std::views::transform([](const auto& pair) {
                                                      const auto& [type, count] = pair;
                                                      return vk::DescriptorPoolSize{
                                                          .type = type,
                                                          .descriptorCount = count,
                                                      };
                                                  }))
                              .data(),
        }};

    descriptor_set_layouts.reserve(descriptor_sets.size());
    for (const auto& set : descriptor_sets) {
        std::vector<vk::DescriptorSetLayoutBinding> bindings;
        bindings.reserve(set.size());

        u32 binding_count = 0;
        for (const auto& binding : set) {
            bindings.emplace_back(vk::DescriptorSetLayoutBinding{
                .binding = binding_count++,
                .descriptorType = binding.type,
                .descriptorCount = binding.count,
                .stageFlags = binding.stages,
            });
        }

        descriptor_set_layouts.emplace_back(*device,
                                            vk::DescriptorSetLayoutCreateInfo{
                                                .bindingCount = static_cast<u32>(bindings.size()),
                                                .pBindings = bindings.data(),
                                            });
    }

    const auto& raw_set_layouts = Common::VectorFromRange(
        descriptor_set_layouts | std::views::transform(&vk::raii::DescriptorSetLayout::operator*));
    for (auto& frame : frames_in_flight) {
        frame.descriptor_sets = vk::raii::DescriptorSets{
            *device, vk::DescriptorSetAllocateInfo{
                         .descriptorPool = *descriptor_pool,
                         .descriptorSetCount = static_cast<u32>(raw_set_layouts.size()),
                         .pSetLayouts = raw_set_layouts.data(),
                     }};
    }

    u32 set_count = 0;
    for (const auto& set : descriptor_sets) {
        u32 binding_count = 0;
        for (const auto& binding : set) {
            UpdateDescriptor(set_count, binding_count, binding, true);
            binding_count++;
        }
        set_count++;
    }

    // Pipeline layouts
    pipeline_layout = vk::raii::PipelineLayout{
        *device,
        {
            .setLayoutCount = static_cast<u32>(raw_set_layouts.size()),
            .pSetLayouts = raw_set_layouts.data(),
            .pushConstantRangeCount = static_cast<u32>(push_constants.size()),
            .pPushConstantRanges = push_constants.data(),
        }};

    // Command buffers
    command_pool =
        vk::raii::CommandPool{*device,
                              {
                                  .flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer,
                                  .queueFamilyIndex = device.graphics_queue_family,
                              }};
    vk::raii::CommandBuffers command_buffers{
        *device,
        {
            .commandPool = *command_pool,
            .level = vk::CommandBufferLevel::ePrimary,
            .commandBufferCount = static_cast<u32>(frames_in_flight.size()),
        }};
    for (std::size_t i = 0; i < frames_in_flight.size(); ++i) {
        frames_in_flight[i].command_buffer = std::move(command_buffers[i]);
    }

    // Sync objects
    for (auto& frame : frames_in_flight) {
        frame.render_start_semaphore = vk::raii::Semaphore{*device, vk::SemaphoreCreateInfo{}};
        frame.render_finished_semaphore = vk::raii::Semaphore{*device, vk::SemaphoreCreateInfo{}};
        frame.in_flight_fence = vk::raii::Fence{*device,
                                                {
                                                    .flags = vk::FenceCreateFlagBits::eSignaled,
                                                }};
    }
}

VulkanPipeline::~VulkanPipeline() = default;

void VulkanPipeline::WriteUniformObject(const u8* data, std::size_t size, std::size_t idx,
                                        std::size_t array_idx) {

    const auto& frame = frames_in_flight[current_frame];

    assert(idx < frame.uniform_buffers.size() && array_idx < frame.uniform_buffers[idx].size());
    assert(frame.uniform_buffers[idx][array_idx].buffer->dst_buffer.allocation_info.size == size);
    std::memcpy(**frame.uniform_buffers[idx][array_idx].buffer, data, size);
}

FrameInFlight& VulkanPipeline::AcquireNextFrame() {
    current_frame = (current_frame + 1) % frames_in_flight.size();

    auto& frame = frames_in_flight[current_frame];
    if (device->waitForFences({*frame.in_flight_fence}, VK_TRUE, std::numeric_limits<u64>::max()) !=
        vk::Result::eSuccess) {

        throw std::runtime_error("Failed to wait for fences");
    }
    frame.command_buffer.reset();
    return frame;
}

void VulkanPipeline::BeginFrame() {
    auto& frame = frames_in_flight[current_frame];
    frame.command_buffer.begin({});

    for (const auto& uniform_array : frame.uniform_buffers) {
        for (const auto& [uniform, dst_stages] : uniform_array) {
            uniform->Upload(frame.command_buffer, dst_stages);
        }
    }
}

void VulkanPipeline::EndFrame() {
    auto& frame = frames_in_flight[current_frame];

    frame.command_buffer.end();
    device->resetFences({*frame.in_flight_fence});
}

void VulkanPipeline::UpdateDescriptor(u32 set_idx, u32 binding_idx,
                                      const DescriptorBinding& binding, bool create) {
    for (std::size_t i = 0; i < frames_in_flight.size(); ++i) {
        auto& frame = frames_in_flight[i];

        if (!binding.images.empty()) { // Images
            device->updateDescriptorSets(
                {{
                    .dstSet = *frame.descriptor_sets[set_idx],
                    .dstBinding = binding_idx,
                    .descriptorCount = static_cast<u32>(binding.images.size()),
                    .descriptorType = binding.type,
                    .pImageInfo =
                        Common::VectorFromRange(
                            binding.images | std::views::transform([this, i](const auto& image) {
                                return vk::DescriptorImageInfo{
                                    .sampler = *sampler,
                                    .imageView = image.images.size() > i
                                                     ? *(image.images.begin() + i)
                                                     : *image.images.begin(),
                                    .imageLayout = image.layout,
                                };
                            }))
                            .data(),
                }},
                {});
        } else if (!binding.buffers.empty()) { // Buffers
            device->updateDescriptorSets(
                {{
                    .dstSet = *frame.descriptor_sets[set_idx],
                    .dstBinding = binding_idx,
                    .descriptorCount = static_cast<u32>(binding.buffers.size()),
                    .descriptorType = binding.type,
                    .pBufferInfo =
                        Common::VectorFromRange(
                            binding.buffers | std::views::transform([i](const auto& buffer) {
                                return vk::DescriptorBufferInfo{
                                    .buffer = buffer.buffers.size() > i
                                                  ? *(buffer.buffers.begin() + i)
                                                  : *buffer.buffers.begin(),
                                    .offset = 0,
                                    .range = VK_WHOLE_SIZE,
                                };
                            }))
                            .data(),
                }},
                {});
        } else if (create && binding.type == vk::DescriptorType::eUniformBuffer &&
                   binding.size != 0) { // Create

            frame.uniform_buffers.emplace_back();
            for (u32 i = 0; i < binding.count; ++i) {
                frame.uniform_buffers.back().emplace_back(FrameInFlight::UniformBuffer{
                    std::make_unique<VulkanUniformBuffer>(*device.allocator, binding.size),
                    ToPipelineStages(binding.stages),
                });
            }
            device->updateDescriptorSets(
                {{
                    .dstSet = *frame.descriptor_sets[set_idx],
                    .dstBinding = binding_idx,
                    .descriptorCount = binding.count,
                    .descriptorType = binding.type,
                    .pBufferInfo =
                        Common::VectorFromRange(frame.uniform_buffers.back() |
                                                std::views::transform([](const auto& uniform) {
                                                    return vk::DescriptorBufferInfo{
                                                        .buffer = *uniform.buffer->dst_buffer,
                                                        .offset = 0,
                                                        .range = VK_WHOLE_SIZE,
                                                    };
                                                }))
                            .data(),
                }},
                {});
        }
    }
}

} // namespace Renderer
