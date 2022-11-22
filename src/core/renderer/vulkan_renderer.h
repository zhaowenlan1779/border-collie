// Copyright 2022 Pengfei Zhu
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <memory>
#include <utility>
#include <vector>
#include <glm/glm.hpp>
#include <vulkan/vulkan_raii.hpp>
#include "common/common_types.h"
#include "core/renderer/vulkan_allocator.h"
#include "core/renderer/vulkan_buffer.h"

/**
 * Base class for Vulkan based renderers, managing the Vulkan objects.
 */
class VulkanRenderer {
public:
    explicit VulkanRenderer(bool enable_validation_layers,
                            std::vector<const char*> frontend_required_extensions);
    ~VulkanRenderer();

    vk::raii::Instance& GetVulkanInstance();
    const vk::raii::Instance& GetVulkanInstance() const;

    void Init(vk::SurfaceKHR surface, const vk::Extent2D& actual_extent);
    void DrawFrame();
    void RecreateSwapchain(const vk::Extent2D& actual_extent);

private:
    void CreateInstance(bool enable_validation_layers,
                        const std::vector<const char*>& frontend_required_extensions);
    void InitDevice();
    bool CreateDevice();
    void CreateSwapchain(const vk::Extent2D& actual_extent);
    void CreateRenderPass();
    void CreateGraphicsPipeline();
    void CreateFramebuffers();
    void CreateCommandBuffers();
    void CreateVertexBuffer();
    void CreateSyncObjects();
    void RecordCommands(vk::raii::CommandBuffer& command_buffer, std::size_t image_index);

    vk::raii::Context context;
    vk::raii::Instance instance = nullptr;
    vk::raii::DebugUtilsMessengerEXT debug_messenger = nullptr;

    vk::raii::SurfaceKHR surface = nullptr;
    vk::raii::PhysicalDevice physical_device = nullptr;
    vk::raii::Device device = nullptr;

    vk::raii::Queue graphics_queue = nullptr;
    u32 graphics_queue_family = 0;
    vk::raii::Queue present_queue = nullptr;
    u32 present_queue_family = 0;
    std::vector<u32> queue_family_indices;

    vk::SurfaceFormatKHR surface_format{};
    vk::raii::SwapchainKHR swap_chain = nullptr;
    vk::Extent2D extent{};
    std::vector<vk::raii::ImageView> image_views;

    vk::raii::RenderPass render_pass = nullptr;
    vk::raii::PipelineLayout pipeline_layout = nullptr;
    vk::raii::Pipeline pipeline = nullptr;

    std::vector<vk::raii::Framebuffer> framebuffers;

    vk::raii::CommandPool command_pool = nullptr;
    std::unique_ptr<VulkanAllocator> allocator{};
    std::unique_ptr<VulkanBuffer> vertex_buffer{};
    std::unique_ptr<VulkanBuffer> vertex_staging_buffer{};

    struct FrameInFlight {
        vk::raii::CommandBuffer command_buffer = nullptr;
        vk::raii::Semaphore image_available_semaphore = nullptr;
        vk::raii::Semaphore render_finished_semaphore = nullptr;
        vk::raii::Fence in_flight_fence = nullptr;
    };
    std::array<FrameInFlight, 2> frames_in_flight;
    std::size_t current_frame = 0;
};
