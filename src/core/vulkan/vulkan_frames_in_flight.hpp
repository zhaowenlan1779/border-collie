// Copyright 2022 Pengfei Zhu
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <array>
#include <map>
#include <memory>
#include <utility>
#include <vector>
#include <vulkan/vulkan_raii.hpp>
#include "common/common_types.h"
#include "common/ranges.h"
#include "core/vulkan/vulkan_device.h"
#include "core/vulkan/vulkan_helpers.hpp"

namespace Renderer {

template <typename T>
constexpr vk::PushConstantRange PushConstant(vk::ShaderStageFlags stages) {
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

    // Only one of the following 3 should be set
    struct BufferRef {
        vk::ArrayProxy<const vk::Buffer> buffers;
    };
    vk::ArrayProxy<const BufferRef> buffers;

    struct ImageRef {
        vk::ArrayProxy<const vk::ImageView> images;
        vk::ImageLayout layout;
    };
    vk::ArrayProxy<const ImageRef> images;
    vk::ArrayProxy<const vk::AccelerationStructureKHR> accel_structures;
};

using DescriptorSet = vk::ArrayProxy<const DescriptorBinding>;

template <typename ExtraData>
struct FrameInFlight : NonCopyable {
    vk::raii::CommandBuffer command_buffer = nullptr;
    vk::raii::Semaphore render_finished_semaphore = nullptr;
    vk::raii::Fence in_flight_fence = nullptr;
    std::vector<vk::raii::DescriptorSet> descriptor_sets;
    ExtraData extras;
};

/**
 * Base class for a pipeline. Handles descriptors, push constants, frames in flight, etc.
 */
template <typename ExtraData, std::size_t NumFramesInFlight>
class VulkanFramesInFlight : NonCopyable {
public:
    explicit VulkanFramesInFlight(const VulkanDevice& device_) : device(device_) {
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
            frame.render_finished_semaphore =
                vk::raii::Semaphore{*device, vk::SemaphoreCreateInfo{}};
            frame.in_flight_fence = vk::raii::Fence{*device,
                                                    {
                                                        .flags = vk::FenceCreateFlagBits::eSignaled,
                                                    }};
        }
    }

    ~VulkanFramesInFlight() = default;

    void CreateDescriptors(const vk::ArrayProxy<const DescriptorSet>& descriptor_sets) {
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

            descriptor_set_layouts.emplace_back(
                *device, vk::DescriptorSetLayoutCreateInfo{
                             .bindingCount = static_cast<u32>(bindings.size()),
                             .pBindings = bindings.data(),
                         });
        }

        const auto& raw_set_layouts = Common::VectorFromRange(
            descriptor_set_layouts |
            std::views::transform(&vk::raii::DescriptorSetLayout::operator*));
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
                UpdateDescriptor(set_count, binding_count, binding);
                binding_count++;
            }
            set_count++;
        }
    }

    void UpdateDescriptor(u32 set_idx, u32 binding_idx, const DescriptorBinding& binding) const {
        for (std::size_t i = 0; i < frames_in_flight.size(); ++i) {
            const auto& frame = frames_in_flight[i];

            if (!binding.images.empty()) { // Images
                device->updateDescriptorSets(
                    {{
                        .dstSet = *frame.descriptor_sets[set_idx],
                        .dstBinding = binding_idx,
                        .descriptorCount = static_cast<u32>(binding.images.size()),
                        .descriptorType = binding.type,
                        .pImageInfo = Common::VectorFromRange(
                                          binding.images |
                                          std::views::transform([this, i](const auto& image) {
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
            } else if (!binding.accel_structures.empty()) { // Accel structures
                device->updateDescriptorSets(
                    {Helpers::GenericStructureChain{
                        vk::WriteDescriptorSet{
                            .dstSet = *frame.descriptor_sets[set_idx],
                            .dstBinding = binding_idx,
                            .descriptorCount = static_cast<u32>(binding.accel_structures.size()),
                            .descriptorType = binding.type,
                        },
                        vk::WriteDescriptorSetAccelerationStructureKHR{
                            .accelerationStructureCount =
                                static_cast<u32>(binding.accel_structures.size()),
                            .pAccelerationStructures = binding.accel_structures.data(),
                        },
                    }},
                    {});
            }
        }
    }

    FrameInFlight<ExtraData>& AcquireNextFrame() {
        current_frame = (current_frame + 1) % frames_in_flight.size();

        auto& frame = frames_in_flight[current_frame];
        if (device->waitForFences({*frame.in_flight_fence}, VK_TRUE,
                                  std::numeric_limits<u64>::max()) != vk::Result::eSuccess) {

            throw std::runtime_error("Failed to wait for fences");
        }
        frame.command_buffer.reset();
        return frame;
    }

    void BeginFrame() const {
        const auto& frame = frames_in_flight[current_frame];
        frame.command_buffer.begin({});
    }

    vk::raii::PipelineLayout CreatePipelineLayout(
        const vk::ArrayProxy<const std::size_t>& set_indices,
        const vk::ArrayProxy<const vk::PushConstantRange>& push_constants) const {

        return vk::raii::PipelineLayout{
            *device,
            {
                .setLayoutCount = static_cast<u32>(set_indices.size()),
                .pSetLayouts = Common::VectorFromRange(
                                   set_indices | std::views::transform([this](std::size_t index) {
                                       return *descriptor_set_layouts[index];
                                   }))
                                   .data(),
                .pushConstantRangeCount = static_cast<u32>(push_constants.size()),
                .pPushConstantRanges = push_constants.data(),
            }};
    }

    // Default to all descriptor sets
    vk::raii::PipelineLayout CreatePipelineLayout(
        const vk::ArrayProxy<const vk::PushConstantRange>& push_constants) const {
        return vk::raii::PipelineLayout{
            *device,
            {
                .setLayoutCount = static_cast<u32>(descriptor_set_layouts.size()),
                .pSetLayouts = Common::VectorFromRange(
                                   descriptor_set_layouts |
                                   std::views::transform(&vk::raii::DescriptorSetLayout::operator*))
                                   .data(),
                .pushConstantRangeCount = static_cast<u32>(push_constants.size()),
                .pPushConstantRanges = push_constants.data(),
            }};
    }

    std::vector<vk::DescriptorSet> GetDescriptorSets(
        vk::ArrayProxy<const std::size_t> descriptor_set_indices) const {

        const auto& frame = frames_in_flight[current_frame];
        return Common::VectorFromRange(descriptor_set_indices |
                                       std::views::transform([&frame](std::size_t index) {
                                           return *frame.descriptor_sets[index];
                                       }));
    }

    // Default to all sets
    std::vector<vk::DescriptorSet> GetDescriptorSets() const {
        const auto& frame = frames_in_flight[current_frame];
        return Common::VectorFromRange(frame.descriptor_sets |
                                       std::views::transform(&vk::raii::DescriptorSet::operator*));
    }

    void EndFrame() const {
        const auto& frame = frames_in_flight[current_frame];
        frame.command_buffer.end();
        device->resetFences({*frame.in_flight_fence});
    }

    const VulkanDevice& device;
    vk::raii::DescriptorPool descriptor_pool = nullptr;
    std::vector<vk::raii::DescriptorSetLayout> descriptor_set_layouts;

    vk::raii::Sampler sampler = nullptr;
    vk::raii::CommandPool command_pool = nullptr;

    std::array<FrameInFlight<ExtraData>, NumFramesInFlight> frames_in_flight;
    std::size_t current_frame = 0;
};

} // namespace Renderer
