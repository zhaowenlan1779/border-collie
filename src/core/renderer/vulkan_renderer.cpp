// Copyright 2022 Pengfei Zhu
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <algorithm>
#include <array>
#include <chrono>
#include <limits>
#include <ranges>
#include <set>
#include <string_view>
#include <glm/gtc/matrix_transform.hpp>
#include <spdlog/spdlog.h>
#include "common/ranges.h"
#include "common/temp_ptr.h"
#include "core/renderer/vulkan_allocator.h"
#include "core/renderer/vulkan_buffer.h"
#include "core/renderer/vulkan_helpers.hpp"
#include "core/renderer/vulkan_renderer.h"
#include "core/renderer/vulkan_shader.h"

namespace Renderer {

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
    VkDebugUtilsMessageSeverityFlagBitsEXT severity_,
    [[maybe_unused]] VkDebugUtilsMessageTypeFlagsEXT type,
    const VkDebugUtilsMessengerCallbackDataEXT* callback_data, [[maybe_unused]] void* user_data) {

    vk::DebugUtilsMessageSeverityFlagBitsEXT severity{static_cast<u32>(severity_)};
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
    const bool validation_available = std::ranges::all_of(
        validation_layers, [&available_layers](const std::string_view& layer_name) {
            return std::ranges::any_of(available_layers,
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
    CreateDescriptorSetLayout();
    CreateGraphicsPipeline();
    CreateFramebuffers();
    CreateCommandBuffers();
    CreateBuffers();
    CreateDescriptors();
    CreateSyncObjects();
}

void VulkanRenderer::InitDevice() {
    vk::raii::PhysicalDevices physical_devices{instance};

    // Prefer discrete GPUs
    const auto result =
        std::ranges::find_if(physical_devices, [this](vk::raii::PhysicalDevice& it) {
            return it.getProperties().deviceType == vk::PhysicalDeviceType::eDiscreteGpu &&
                   CreateDevice(it);
        });
    if (result != physical_devices.end()) {
        return;
    }

    // Try everything else
    const auto result_2 =
        std::ranges::find_if(physical_devices, [this](vk::raii::PhysicalDevice& it) {
            return it.getProperties().deviceType != vk::PhysicalDeviceType::eDiscreteGpu &&
                   CreateDevice(it);
        });
    if (result_2 == physical_devices.end()) {
        throw std::runtime_error("Failed to create any device");
    }
}

bool VulkanRenderer::CreateDevice(vk::raii::PhysicalDevice& physical_device_) {
    physical_device = std::move(physical_device_);

    // Check for extensions
    static constexpr std::array<const char*, 1> RequiredExtensions{{
        VK_KHR_SWAPCHAIN_EXTENSION_NAME,
    }};

    const auto& device_exts = physical_device.enumerateDeviceExtensionProperties();
    if (!std::ranges::all_of(RequiredExtensions, [&device_exts](const std::string_view ext_name) {
            return std::ranges::any_of(device_exts,
                                       [&ext_name](const vk::ExtensionProperties& ext) {
                                           return ext_name == ext.extensionName;
                                       });
        })) {

        return false;
    }

    const auto& queue_families = physical_device.getQueueFamilyProperties();
    graphics_queue_family = *std::ranges::find_if(
        std::ranges::iota_view<u32, u32>(0, queue_families.size()), [&queue_families](u32 i) {
            return static_cast<bool>(queue_families[i].queueFlags & vk::QueueFlagBits::eGraphics);
        });
    present_queue_family = *std::ranges::find_if(
        std::ranges::iota_view<u32, u32>(0, queue_families.size()),
        [this](u32 i) { return physical_device.getSurfaceSupportKHR(i, *surface); });

    if (graphics_queue_family == queue_families.size() ||
        present_queue_family == queue_families.size()) {
        return false;
    }

    const std::set<u32> family_ids{graphics_queue_family, present_queue_family};
    float priority = 1.0f;
    try {
        device = vk::raii::Device{
            physical_device,
            {
                .queueCreateInfoCount = static_cast<u32>(family_ids.size()),
                .pQueueCreateInfos =
                    Common::VectorFromRange(family_ids |
                                            std::views::transform([&priority](u32 family_id) {
                                                return vk::DeviceQueueCreateInfo{
                                                    .queueFamilyIndex = family_id,
                                                    .queueCount = 1,
                                                    .pQueuePriorities = &priority,
                                                };
                                            }))
                        .data(),
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

void VulkanRenderer::CreateDescriptorSetLayout() {
    descriptor_set_layout =
        vk::raii::DescriptorSetLayout{device,
                                      {
                                          .bindingCount = 1,
                                          .pBindings = TempArr<vk::DescriptorSetLayoutBinding>{{
                                              .binding = 0,
                                              .descriptorType = vk::DescriptorType::eUniformBuffer,
                                              .descriptorCount = 1,
                                              .stageFlags = vk::ShaderStageFlagBits::eVertex,
                                          }},
                                      }};
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

struct Vertex {
    glm::vec2 pos;
    glm::vec3 color;
};

void VulkanRenderer::CreateGraphicsPipeline() {
    pipeline_layout = vk::raii::PipelineLayout{device,
                                               {
                                                   .setLayoutCount = 1,
                                                   .pSetLayouts = TempArr<vk::DescriptorSetLayout>{{
                                                       *descriptor_set_layout,
                                                   }},
                                               }};

    static constexpr auto VertexAttributeDescriptions = AttributeDescriptionsFor<Vertex>();
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
            .pVertexInputState = TempPtr{vk::PipelineVertexInputStateCreateInfo{
                .vertexBindingDescriptionCount = 1,
                .pVertexBindingDescriptions = TempArr<vk::VertexInputBindingDescription>{{
                    .binding = 0,
                    .stride = sizeof(Vertex),
                    .inputRate = vk::VertexInputRate::eVertex,
                }},
                .vertexAttributeDescriptionCount = VertexAttributeDescriptions.size(),
                .pVertexAttributeDescriptions = VertexAttributeDescriptions.data(),
            }},
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

struct VulkanRenderer::UniformBufferObject {
    glm::mat4 model;
    glm::mat4 view;
    glm::mat4 proj;
};

void VulkanRenderer::CreateBuffers() {
    allocator = std::make_unique<VulkanAllocator>(*instance, *physical_device, *device);

    const std::vector<Vertex> vertices{{{-0.5f, -0.5f}, {1.0f, 0.0f, 0.0f}},
                                       {{0.5f, -0.5f}, {0.0f, 1.0f, 0.0f}},
                                       {{0.5f, 0.5f}, {0.0f, 0.0f, 1.0f}},
                                       {{-0.5f, 0.5f}, {1.0f, 1.0f, 1.0f}}};
    vertex_buffer = std::make_unique<VulkanStagedBuffer>(
        *allocator, reinterpret_cast<const u8*>(vertices.data()),
        vertices.size() * sizeof(vertices[0]), vk::BufferUsageFlagBits::eVertexBuffer);

    const std::vector<u16> indices = {0, 1, 2, 2, 3, 0};
    index_buffer = std::make_unique<VulkanStagedBuffer>(
        *allocator, reinterpret_cast<const u8*>(indices.data()),
        indices.size() * sizeof(indices[0]), vk::BufferUsageFlagBits::eIndexBuffer);

    // Upload staging buffers
    vk::raii::CommandBuffers tmp_cmdbuf{device,
                                        {
                                            .commandPool = *command_pool,
                                            .level = vk::CommandBufferLevel::ePrimary,
                                            .commandBufferCount = 1,
                                        }};

    {
        CommandBufferContext cmd_context{tmp_cmdbuf[0],
                                         {.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit}};
        vertex_buffer->Upload(cmd_context);
        index_buffer->Upload(cmd_context);
    }
    graphics_queue.submit({
        {
            .commandBufferCount = 1,
            .pCommandBuffers = TempArr<vk::CommandBuffer>{*tmp_cmdbuf[0]},
        },
    });

    for (auto& frame : frames_in_flight) {
        frame.uniform_buffer = std::make_unique<VulkanUniformBufferObject<UniformBufferObject>>(
            *allocator, vk::PipelineStageFlagBits2::eVertexShader);
    }

    graphics_queue.waitIdle();
}

void VulkanRenderer::CreateDescriptors() {
    descriptor_pool = vk::raii::DescriptorPool{
        device,
        {
            .maxSets = static_cast<u32>(frames_in_flight.size()),
            .poolSizeCount = 1,
            .pPoolSizes = TempArr<vk::DescriptorPoolSize>{{
                .type = vk::DescriptorType::eUniformBuffer,
                .descriptorCount = static_cast<u32>(frames_in_flight.size()),
            }},
        }};

    vk::raii::DescriptorSets descriptor_sets{
        device,
        {
            .descriptorPool = *descriptor_pool,
            .descriptorSetCount = static_cast<u32>(frames_in_flight.size()),
            .pSetLayouts = std::vector{frames_in_flight.size(), *descriptor_set_layout}.data(),
        }};
    for (std::size_t i = 0; i < frames_in_flight.size(); ++i) {
        frames_in_flight[i].descriptor_set = std::move(descriptor_sets[i]);
    }
    device.updateDescriptorSets(
        WRAPPED_RANGE(frames_in_flight | std::views::transform([](const FrameInFlight& frame) {
                          return TempWrapper<ArrSpec<&vk::WriteDescriptorSet::pBufferInfo,
                                                     &vk::WriteDescriptorSet::descriptorCount>>{
                              {.dstSet = *frame.descriptor_set,
                               .dstBinding = 0,
                               .descriptorCount = 1,
                               .descriptorType = vk::DescriptorType::eUniformBuffer,
                               .pBufferInfo = TempArr<vk::DescriptorBufferInfo>{{
                                   .buffer = frame.uniform_buffer->dst_buffer.buffer,
                                   .offset = 0,
                                   .range = VK_WHOLE_SIZE,
                               }}}};
                      })),
        {});
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

void VulkanRenderer::RecordCommands(FrameInFlight& frame, std::size_t image_index) {
    const auto& cmd = frame.command_buffer;

    CommandBufferContext cmd_context{cmd, {}};
    frame.uniform_buffer->Upload(cmd_context);

    CommandBufferRenderPassContext render_pass_context{
        cmd,
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

    cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *pipeline);
    cmd.bindVertexBuffers(0, {**vertex_buffer}, {0});
    cmd.bindIndexBuffer(**index_buffer, 0, vk::IndexType::eUint16);

    // Dynamic states
    cmd.setViewport(0, {{
                           .x = 0.0f,
                           .y = 0.0f,
                           .width = static_cast<float>(extent.width),
                           .height = static_cast<float>(extent.height),
                           .minDepth = 0.0f,
                           .maxDepth = 1.0f,
                       }});
    cmd.setScissor(0, {{
                          .offset = {},
                          .extent = extent,
                      }});

    cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *pipeline_layout, 0,
                           {*frame.descriptor_set}, {});
    cmd.drawIndexed(6, 1, 0, 0, 0);
}

VulkanRenderer::UniformBufferObject VulkanRenderer::GetUniformBufferObject() const {
    static auto startTime = std::chrono::high_resolution_clock::now();

    const auto currentTime = std::chrono::high_resolution_clock::now();
    const float time =
        std::chrono::duration<float, std::chrono::seconds::period>(currentTime - startTime).count();

    auto proj = glm::perspective(glm::radians(45.0f),
                                 extent.width / static_cast<float>(extent.height), 0.1f, 10.0f);
    proj[1][1] *= -1;
    return {
        .model =
            glm::rotate(glm::mat4(1.0f), time * glm::radians(90.0f), glm::vec3(0.0f, 0.0f, 1.0f)),
        .view = glm::lookAt(glm::vec3(2.0f, 2.0f, 2.0f), glm::vec3(0.0f, 0.0f, 0.0f),
                            glm::vec3(0.0f, 0.0f, 1.0f)),
        .proj = proj,
    };
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

    // Update uniform buffer
    frame.uniform_buffer->Update(GetUniformBufferObject());

    frame.command_buffer.reset();
    RecordCommands(frame, image_index);

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

} // namespace Renderer
