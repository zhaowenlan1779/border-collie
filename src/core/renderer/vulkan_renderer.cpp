// Copyright 2022 Pengfei Zhu
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <algorithm>
#include <array>
#include <limits>
#include <set>
#include <string_view>
#include <spdlog/spdlog.h>
#include "common/temp_ptr.h"
#include "core/renderer/vulkan_raii_helpers.hpp"
#include "core/renderer/vulkan_renderer.h"
#include "core/renderer/vulkan_shader.h"

VulkanRenderer::VulkanRenderer(bool enable_validation_layers,
                               std::vector<const char*> frontend_required_extensions) {

    CreateInstance(enable_validation_layers, frontend_required_extensions);
}

VulkanRenderer::~VulkanRenderer() {
    if (*device) {
        device.waitIdle();
    }
}

static VKAPI_ATTR VkBool32 VKAPI_CALL DebugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT severity_, VkDebugUtilsMessageTypeFlagsEXT type,
    const VkDebugUtilsMessengerCallbackDataEXT* callback_data, void* user_data) {

    vk::DebugUtilsMessageSeverityFlagBitsEXT severity{severity_};
    if (severity == vk::DebugUtilsMessageSeverityFlagBitsEXT::eError) {
        SPDLOG_ERROR("{}", callback_data->pMessage);
    } else if (severity == vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning) {
        SPDLOG_WARN("{}", callback_data->pMessage);
    } else if (severity == vk::DebugUtilsMessageSeverityFlagBitsEXT::eInfo) {
        SPDLOG_INFO("{}", callback_data->pMessage);
    } else {
        SPDLOG_DEBUG("{}", callback_data->pMessage);
    }
    return VK_FALSE;
}

void VulkanRenderer::CreateInstance(bool enable_validation_layers,
                                    const std::vector<const char*>& frontend_required_extensions) {

    // Validation Layers
    std::vector<const char*> validation_layers;
    if (enable_validation_layers) {
        validation_layers.emplace_back("VK_LAYER_KHRONOS_validation");
    }

    const auto& available_layers = context.enumerateInstanceLayerProperties();
    const bool validation_available =
        std::all_of(validation_layers.begin(), validation_layers.end(),
                    [&available_layers](const std::string_view& layer_name) {
                        return std::any_of(available_layers.begin(), available_layers.end(),
                                           [&layer_name](const vk::LayerProperties& layer) {
                                               return layer_name == layer.layerName;
                                           });
                    });
    if (!validation_available) {
        SPDLOG_CRITICAL("Some validation layers are not available");
        throw std::runtime_error("Some validation layers are not available");
    }

    // Extensions
    std::vector<const char*> required_extensions(frontend_required_extensions);
    if (enable_validation_layers) {
        required_extensions.emplace_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    }

    const vk::InstanceCreateInfo instance_info{
        .pApplicationInfo = TempPtr{vk::ApplicationInfo{
            .pApplicationName = "BorderCollie",
            .applicationVersion = 1,
            .pEngineName = "BorderCollie",
            .engineVersion = 1,
            .apiVersion = VK_API_VERSION_1_3,
        }},
        .enabledLayerCount = static_cast<u32>(validation_layers.size()),
        .ppEnabledLayerNames = validation_layers.data(),
        .enabledExtensionCount = static_cast<u32>(required_extensions.size()),
        .ppEnabledExtensionNames = required_extensions.data(),
    };
    const vk::DebugUtilsMessengerCreateInfoEXT debug_messenger_info{
        .messageSeverity = vk::DebugUtilsMessageSeverityFlagBitsEXT::eInfo |
                           vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning |
                           vk::DebugUtilsMessageSeverityFlagBitsEXT::eError,
        .messageType = vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral |
                       vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance |
                       vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation,
        .pfnUserCallback = &DebugCallback,
    };
    if (enable_validation_layers) {
        instance = vk::raii::Instance{
            context,
            vk::StructureChain{instance_info, debug_messenger_info}.get<vk::InstanceCreateInfo>()};
        debug_messenger = vk::raii::DebugUtilsMessengerEXT{instance, debug_messenger_info};
    } else {
        instance = vk::raii::Instance{context, instance_info};
    }
}

vk::raii::Instance& VulkanRenderer::GetVulkanInstance() {
    return instance;
}

const vk::raii::Instance& VulkanRenderer::GetVulkanInstance() const {
    return instance;
}

void VulkanRenderer::Init(vk::SurfaceKHR surface_, const vk::Extent2D& actual_extent) {
    surface = vk::raii::SurfaceKHR{instance, surface_};

    InitDevice();
    CreateSwapchain(actual_extent);
    CreateRenderPass();
    CreateGraphicsPipeline();
    CreateFramebuffers();
    CreateCommandBuffers();
    CreateSyncObjects();
}

void VulkanRenderer::InitDevice() {
    const vk::raii::PhysicalDevices physical_devices{instance};

    // Prefer discrete GPUs
    for (auto it : physical_devices) {
        if (it.getProperties().deviceType == vk::PhysicalDeviceType::eDiscreteGpu) {
            physical_device = it;
            if (CreateDevice()) {
                return;
            }
        }
    }
    for (auto it : physical_devices) {
        if (it.getProperties().deviceType != vk::PhysicalDeviceType::eDiscreteGpu) {
            physical_device = it;
            if (CreateDevice()) {
                return;
            }
        }
    }
    throw std::runtime_error("Failed to create any device");
}

bool VulkanRenderer::CreateDevice() {
    // Check for extensions
    static constexpr std::array<const char*, 1> RequiredExtensions{{
        VK_KHR_SWAPCHAIN_EXTENSION_NAME,
    }};

    const auto& device_exts = physical_device.enumerateDeviceExtensionProperties();
    for (const std::string_view ext_name : RequiredExtensions) {
        const bool ext_supported = std::any_of(device_exts.begin(), device_exts.end(),
                                               [&ext_name](const vk::ExtensionProperties& ext) {
                                                   return ext_name == ext.extensionName;
                                               });
        if (!ext_supported) {
            return false;
        }
    }

    const auto& queue_families = physical_device.getQueueFamilyProperties();

    graphics_queue_family = present_queue_family = queue_families.size();
    for (std::size_t i = 0; i < queue_families.size(); ++i) {
        if (queue_families[i].queueFlags & vk::QueueFlagBits::eGraphics) {
            graphics_queue_family = i;
        }
        if (physical_device.getSurfaceSupportKHR(i, *surface)) {
            present_queue_family = i;
        }
    }
    if (graphics_queue_family == queue_families.size() ||
        present_queue_family == queue_families.size()) {
        return false;
    }

    const std::set<u32> family_ids{graphics_queue_family, present_queue_family};
    std::vector<vk::DeviceQueueCreateInfo> queues;
    for (const u32 family_id : family_ids) {
        queues.emplace_back(vk::DeviceQueueCreateInfo{
            .queueFamilyIndex = family_id,
            .queueCount = 1,
            .pQueuePriorities = TempArr<float>{1.0f},
        });
    }

    try {
        device = vk::raii::Device{
            physical_device,
            {
                .queueCreateInfoCount = static_cast<u32>(queues.size()),
                .pQueueCreateInfos = queues.data(),
                .enabledExtensionCount = static_cast<u32>(RequiredExtensions.size()),
                .ppEnabledExtensionNames = RequiredExtensions.data(),
            }};
    } catch (...) {
        return false;
    }

    queue_family_indices.assign(family_ids.begin(), family_ids.end());
    graphics_queue = device.getQueue(graphics_queue_family, 0);
    present_queue = device.getQueue(present_queue_family, 0);
    return true;
}

static vk::SurfaceFormatKHR SelectSurfaceFormat(const std::vector<vk::SurfaceFormatKHR>& formats) {
    for (const auto format : formats) {
        if (format.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear &&
            format.format == vk::Format::eB8G8R8A8Srgb) {
            return format;
        }
    }
    return formats[0];
}

static vk::PresentModeKHR SelectPresentMode(const std::vector<vk::PresentModeKHR>& present_modes) {
    for (const auto mode : present_modes) {
        if (mode == vk::PresentModeKHR::eMailbox) {
            return mode;
        }
    }
    return present_modes[0];
}

void VulkanRenderer::CreateSwapchain(const vk::Extent2D& actual_extent) {
    surface_format = SelectSurfaceFormat(physical_device.getSurfaceFormatsKHR(*surface));
    const auto present_mode =
        SelectPresentMode(physical_device.getSurfacePresentModesKHR(*surface));

    const auto& capabilities = physical_device.getSurfaceCapabilitiesKHR(*surface);
    extent = vk::Extent2D{
        std::clamp(extent.width, capabilities.minImageExtent.width,
                   capabilities.maxImageExtent.width),
        std::clamp(extent.height, capabilities.minImageExtent.height,
                   capabilities.maxImageExtent.height),
    };
    const u32 image_count = std::min(capabilities.minImageCount + 1, capabilities.maxImageCount);
    swap_chain = vk::raii::SwapchainKHR{
        device,
        {
            .surface = *surface,
            .minImageCount = image_count,
            .imageFormat = surface_format.format,
            .imageColorSpace = surface_format.colorSpace,
            .imageExtent = std::move(actual_extent),
            .imageArrayLayers = 1,
            .imageUsage = vk::ImageUsageFlagBits::eColorAttachment,
            .imageSharingMode = queue_family_indices.size() > 1 ? vk::SharingMode::eConcurrent
                                                                : vk::SharingMode::eExclusive,
            .queueFamilyIndexCount = static_cast<u32>(queue_family_indices.size()),
            .pQueueFamilyIndices = queue_family_indices.data(),
            .preTransform = capabilities.currentTransform,
            .presentMode = present_mode,
            .oldSwapchain = *swap_chain,
        }};

    image_views.clear();
    for (const auto& image : swap_chain.getImages()) {
        image_views.emplace_back(
            device, vk::ImageViewCreateInfo{
                        .image = image,
                        .viewType = vk::ImageViewType::e2D,
                        .format = surface_format.format,
                        .subresourceRange = {.aspectMask = vk::ImageAspectFlagBits::eColor,
                                             .baseMipLevel = 0,
                                             .levelCount = 1,
                                             .baseArrayLayer = 0,
                                             .layerCount = 1}});
    }
}

void VulkanRenderer::CreateRenderPass() {
    render_pass = vk::raii::RenderPass{
        device,
        {
            .attachmentCount = 1,
            .pAttachments = TempArr<vk::AttachmentDescription>{{
                .format = surface_format.format,
                .samples = vk::SampleCountFlagBits::e1,
                .loadOp = vk::AttachmentLoadOp::eClear,
                .storeOp = vk::AttachmentStoreOp::eStore,
                .stencilLoadOp = vk::AttachmentLoadOp::eDontCare,
                .stencilStoreOp = vk::AttachmentStoreOp::eDontCare,
                .finalLayout = vk::ImageLayout::ePresentSrcKHR,
            }},
            .subpassCount = 1,
            .pSubpasses = TempArr<vk::SubpassDescription>{{
                .pipelineBindPoint = vk::PipelineBindPoint::eGraphics,
                .colorAttachmentCount = 1,
                .pColorAttachments = TempArr<vk::AttachmentReference>{{
                    .attachment = 0,
                    .layout = vk::ImageLayout::eColorAttachmentOptimal,
                }},
            }},
            .dependencyCount = 1,
            .pDependencies = TempArr<vk::SubpassDependency>{{
                .srcSubpass = VK_SUBPASS_EXTERNAL,
                .dstSubpass = 0,
                .srcStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput,
                .dstStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput,
                .srcAccessMask = vk::AccessFlags{},
                .dstAccessMask = vk::AccessFlagBits::eColorAttachmentWrite,
            }},
        }};
}

void VulkanRenderer::CreateGraphicsPipeline() {
    pipeline_layout = vk::raii::PipelineLayout{device, vk::PipelineLayoutCreateInfo{}};

    pipeline = vk::raii::Pipeline{
        device,
        VK_NULL_HANDLE,
        {
            .stageCount = 2,
            .pStages = TempArr<vk::PipelineShaderStageCreateInfo>{{
                {
                    .stage = vk::ShaderStageFlagBits::eVertex,
                    .module = *VulkanShader{device, u8"core/renderer/shaders/test.vert"},
                    .pName = "main",
                },
                {
                    .stage = vk::ShaderStageFlagBits::eFragment,
                    .module = *VulkanShader{device, u8"core/renderer/shaders/test.frag"},
                    .pName = "main",
                },
            }},
            .pVertexInputState = TempPtr{vk::PipelineVertexInputStateCreateInfo{}},
            .pInputAssemblyState = TempPtr{vk::PipelineInputAssemblyStateCreateInfo{
                .topology = vk::PrimitiveTopology::eTriangleList,
                .primitiveRestartEnable = VK_FALSE,
            }},
            .pViewportState = TempPtr{vk::PipelineViewportStateCreateInfo{
                .viewportCount = 1,
                .scissorCount = 1,
            }},
            .pRasterizationState = TempPtr{vk::PipelineRasterizationStateCreateInfo{
                .cullMode = vk::CullModeFlagBits::eBack,
                .lineWidth = 1.0f,
            }},
            .pMultisampleState = TempPtr{vk::PipelineMultisampleStateCreateInfo{}},
            .pColorBlendState = TempPtr{vk::PipelineColorBlendStateCreateInfo{
                .logicOpEnable = VK_FALSE,
                .attachmentCount = 1,
                .pAttachments = TempArr<vk::PipelineColorBlendAttachmentState>{{
                    .colorWriteMask =
                        vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
                        vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA,
                }},
            }},
            .pDynamicState = TempPtr{vk::PipelineDynamicStateCreateInfo{
                .dynamicStateCount = 2,
                .pDynamicStates = TempArr<vk::DynamicState>{{
                    vk::DynamicState::eViewport,
                    vk::DynamicState::eScissor,
                }},
            }},
            .layout = *pipeline_layout,
            .renderPass = *render_pass,
            .subpass = 0,
        }};
}

void VulkanRenderer::CreateFramebuffers() {
    framebuffers.clear();
    for (const auto& image_view : image_views) {
        framebuffers.emplace_back(device, vk::FramebufferCreateInfo{
                                              .renderPass = *render_pass,
                                              .attachmentCount = 1,
                                              .pAttachments = TempArr<vk::ImageView>{*image_view},
                                              .width = extent.width,
                                              .height = extent.height,
                                              .layers = 1,
                                          });
    }
}

void VulkanRenderer::CreateCommandBuffers() {
    command_pool =
        vk::raii::CommandPool{device,
                              {
                                  .flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer,
                                  .queueFamilyIndex = graphics_queue_family,
                              }};

    vk::raii::CommandBuffers command_buffers{
        device,
        {
            .commandPool = *command_pool,
            .level = vk::CommandBufferLevel::ePrimary,
            .commandBufferCount = static_cast<u32>(frames_in_flight.size()),
        }};
    for (std::size_t i = 0; i < frames_in_flight.size(); ++i) {
        frames_in_flight[i].command_buffer = std::move(command_buffers[i]);
    }
}

void VulkanRenderer::CreateSyncObjects() {
    for (auto& frame : frames_in_flight) {
        frame.image_available_semaphore = vk::raii::Semaphore{device, vk::SemaphoreCreateInfo{}};
        frame.render_finished_semaphore = vk::raii::Semaphore(device, vk::SemaphoreCreateInfo{});
        frame.in_flight_fence = vk::raii::Fence{device,
                                                {
                                                    .flags = vk::FenceCreateFlagBits::eSignaled,
                                                }};
    }
}

void VulkanRenderer::RecordCommands(vk::raii::CommandBuffer& command_buffer,
                                    std::size_t image_index) {
    CommandBufferContext context{command_buffer, {}};

    CommandBufferRenderPassContext render_pass_context{
        command_buffer,
        {
            .renderPass = *render_pass,
            .framebuffer = *framebuffers[image_index],
            .renderArea =
                {
                    .offset = {},
                    .extent = extent,
                },
            .clearValueCount = 1,
            .pClearValues =
                TempArr<vk::ClearValue>{
                    {{{{0.0f, 0.0f, 0.0f, 1.0f}}}},
                },
        },
        vk::SubpassContents::eInline};

    command_buffer.bindPipeline(vk::PipelineBindPoint::eGraphics, *pipeline);

    // Dynamic states
    command_buffer.setViewport(0, {{
                                      .x = 0.0f,
                                      .y = 0.0f,
                                      .width = static_cast<float>(extent.width),
                                      .height = static_cast<float>(extent.height),
                                      .minDepth = 0.0f,
                                      .maxDepth = 1.0f,
                                  }});
    command_buffer.setScissor(0, {{
                                     .offset = {},
                                     .extent = extent,
                                 }});

    command_buffer.draw(3, 1, 0, 0);
}

void VulkanRenderer::DrawFrame() {
    auto& frame = frames_in_flight[current_frame];
    if (device.waitForFences({*frame.in_flight_fence}, VK_TRUE, std::numeric_limits<u64>::max()) !=
        vk::Result::eSuccess) {

        throw std::runtime_error("Failed to wait for fences");
    }

    const auto& [result, image_index] = swap_chain.acquireNextImage(
        std::numeric_limits<u64>::max(), *frame.image_available_semaphore);
    if (result == vk::Result::eErrorOutOfDateKHR) {
        SPDLOG_WARN("Swapchain is out of date, ignoring frame");
        return;
    } else if (result != vk::Result::eSuccess) {
        throw std::runtime_error("Failed to acquire next image");
    }

    device.resetFences({*frame.in_flight_fence});

    frame.command_buffer.reset();
    RecordCommands(frame.command_buffer, image_index);
    graphics_queue.submit(
        {{.waitSemaphoreCount = 1,
          .pWaitSemaphores = TempArr<vk::Semaphore>{*frame.image_available_semaphore},
          .pWaitDstStageMask =
              TempArr<vk::PipelineStageFlags>{vk::PipelineStageFlagBits::eColorAttachmentOutput},
          .commandBufferCount = 1,
          .pCommandBuffers = TempArr<vk::CommandBuffer>{*frame.command_buffer},
          .signalSemaphoreCount = 1,
          .pSignalSemaphores = TempArr<vk::Semaphore>{*frame.render_finished_semaphore}}},
        *frame.in_flight_fence);

    const auto present_result = present_queue.presentKHR({
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = TempArr<vk::Semaphore>{*frame.render_finished_semaphore},
        .swapchainCount = 1,
        .pSwapchains = TempArr<vk::SwapchainKHR>{*swap_chain},
        .pImageIndices = TempArr<u32>{image_index},
    });
    if (present_result != vk::Result::eSuccess) {
        throw std::runtime_error("Failed to present");
    }

    current_frame = (current_frame + 1) % frames_in_flight.size();
}

void VulkanRenderer::RecreateSwapchain(const vk::Extent2D& actual_extent) {
    device.waitIdle();

    CreateSwapchain(actual_extent);
    CreateFramebuffers();
}
