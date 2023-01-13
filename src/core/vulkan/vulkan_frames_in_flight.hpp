// Copyright 2022 Pengfei Zhu
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <array>
#include <memory>
#include <utility>
#include <vulkan/vulkan_raii.hpp>
#include "common/common_types.h"
#include "core/vulkan/vulkan_device.h"
#include "core/vulkan/vulkan_helpers.hpp"

namespace Renderer {

template <typename ExtraData>
struct FrameInFlight : NonCopyable {
    std::size_t idx{};
    vk::raii::CommandBuffer command_buffer = nullptr;
    vk::raii::Semaphore render_finished_semaphore = nullptr;
    vk::raii::Fence in_flight_fence = nullptr;
    ExtraData extras;
};

/**
 * Base class for a pipeline. Handles descriptors, push constants, frames in flight, etc.
 */
template <typename ExtraData, std::size_t NumFramesInFlight>
class VulkanFramesInFlight : NonCopyable {
public:
    explicit VulkanFramesInFlight(const VulkanDevice& device_) : device(device_) {
        // Command buffers
        vk::raii::CommandBuffers command_buffers{
            *device,
            {
                .commandPool = *device.command_pool,
                .level = vk::CommandBufferLevel::ePrimary,
                .commandBufferCount = static_cast<u32>(frames_in_flight.size()),
            }};
        for (std::size_t i = 0; i < frames_in_flight.size(); ++i) {
            frames_in_flight[i].idx = i;
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

    void EndFrame() const {
        const auto& frame = frames_in_flight[current_frame];
        frame.command_buffer.end();
        device->resetFences({*frame.in_flight_fence});
    }

    const VulkanDevice& device;
    std::array<FrameInFlight<ExtraData>, NumFramesInFlight> frames_in_flight;
    std::size_t current_frame = 0;
};

} // namespace Renderer
