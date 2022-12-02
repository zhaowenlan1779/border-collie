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

namespace Renderer {

class VulkanAllocator;
class VulkanImmUploadBuffer;
class VulkanTexture;
template <typename T>
class VulkanUniformBufferObject;

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
    bool CreateDevice(vk::raii::PhysicalDevice& physical_device);
    void CreateSwapchain(const vk::Extent2D& actual_extent);
    void CreateRenderPass();
    void CreateDescriptorSetLayout();
    void CreateGraphicsPipeline();
    void CreateFramebuffers();
    void CreateCommandBuffers();
    void CreateBuffers();
    void CreateTexture();
    void CreateDescriptors();
    void CreateSyncObjects();

    struct FrameInFlight;
    void RecordCommands(FrameInFlight& frame, std::size_t image_index);
    struct UniformBufferObject;
    UniformBufferObject GetUniformBufferObject() const;
    glm::mat4 GetPushConstant() const;

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
    vk::raii::DescriptorSetLayout descriptor_set_layout = nullptr;
    vk::raii::PipelineLayout pipeline_layout = nullptr;
    vk::raii::Pipeline pipeline = nullptr;

    std::vector<vk::raii::Framebuffer> framebuffers;

    vk::raii::CommandPool command_pool = nullptr;
    std::unique_ptr<VulkanAllocator> allocator{};
    std::unique_ptr<VulkanImmUploadBuffer> vertex_buffer{};
    std::unique_ptr<VulkanImmUploadBuffer> index_buffer{};

    std::unique_ptr<VulkanTexture> texture{};
    vk::raii::Sampler sampler = nullptr;

    vk::raii::DescriptorPool descriptor_pool = nullptr;
    struct FrameInFlight {
        vk::raii::CommandBuffer command_buffer = nullptr;
        vk::raii::Semaphore image_available_semaphore = nullptr;
        vk::raii::Semaphore render_finished_semaphore = nullptr;
        vk::raii::Fence in_flight_fence = nullptr;
        std::unique_ptr<VulkanUniformBufferObject<UniformBufferObject>> uniform_buffer{};
        vk::raii::DescriptorSet descriptor_set = nullptr;
    };
    std::array<FrameInFlight, 2> frames_in_flight;
    std::size_t current_frame = 0;
};

} // namespace Renderer
