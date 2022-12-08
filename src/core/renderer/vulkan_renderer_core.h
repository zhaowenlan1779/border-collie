// Copyright 2022 Pengfei Zhu
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <vulkan/vulkan.hpp>
#include "common/common_types.h"

namespace Renderer {

struct FrameInFlight;

class VulkanRendererCore : NonCopyable {
public:
    virtual ~VulkanRendererCore() = default;
    virtual void OnResized(const vk::Extent2D& actual_extent){};
    virtual const FrameInFlight& DrawFrame() = 0;
};

} // namespace Renderer
