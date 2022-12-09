// Copyright 2022 Pengfei Zhu
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <memory>
#include <utility>
#include <vector>
#include <vulkan/vulkan_raii.hpp>
#include "core/vulkan_renderer.h"

namespace Renderer {

class VulkanPathTracerHW : public VulkanRenderer {
public:
    explicit VulkanPathTracerHW(bool enable_validation_layers,
                                std::vector<const char*> frontend_required_extensions);
    ~VulkanPathTracerHW() override;

    void Init(vk::SurfaceKHR surface, const vk::Extent2D& actual_extent) override;
    void OnResized(const vk::Extent2D& actual_extent) override;

private:
    std::unique_ptr<VulkanDevice> CreateDevice(vk::SurfaceKHR surface,
                                               const vk::Extent2D& actual_extent) const override;
    const FrameInFlight& DrawFrameOffscreen() override;
};

} // namespace Renderer
