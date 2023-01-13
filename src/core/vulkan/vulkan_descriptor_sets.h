// Copyright 2023 Pengfei Zhu
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <memory>
#include <utility>
#include <variant>
#include <vector>
#include <vulkan/vulkan_raii.hpp>
#include "common/common_types.h"
#include "core/vulkan/vulkan_helpers.hpp"

namespace Renderer {

class VulkanDevice;

struct DescriptorBinding {
    vk::DescriptorType type{};
    u32 array_size = 1;
    vk::ShaderStageFlags stages{};

    struct CombinedImageSampler {
        vk::ImageView image;
        vk::Sampler sampler = nullptr;
        vk::ImageLayout layout = vk::ImageLayout::eShaderReadOnlyOptimal;
    };

    // This is to avoid the quirks of nested brace-init-lists
    struct Buffers {
        std::vector<vk::Buffer> buffers;
    };
    struct CombinedImageSamplers {
        std::vector<CombinedImageSampler> images;
    };
    struct AccelStructures {
        std::vector<vk::AccelerationStructureKHR> accel_structures;
    };
    using BuffersValue = vk::ArrayProxy<const Buffers>;
    using CombinedImageSamplersValue = vk::ArrayProxy<const CombinedImageSamplers>;
    using AccelStructuresValue = vk::ArrayProxy<const AccelStructures>;
    using DescriptorBindingValue = std::variant<std::monostate, BuffersValue,
                                                CombinedImageSamplersValue, AccelStructuresValue>;
    DescriptorBindingValue value;
};

// A set layout, descriptor pool, and array of descriptor sets.
class VulkanDescriptorSets : NonCopyable {
public:
    explicit VulkanDescriptorSets(const VulkanDevice& device, std::size_t count,
                                  const vk::ArrayProxy<const DescriptorBinding>& bindings);
    ~VulkanDescriptorSets();

    void UpdateDescriptor(std::size_t binding_idx,
                          const DescriptorBinding::DescriptorBindingValue& value);

    const VulkanDevice& device;
    std::size_t count{};
    std::vector<vk::DescriptorSetLayoutBinding> binding_info;
    vk::raii::DescriptorPool descriptor_pool = nullptr;
    vk::raii::DescriptorSetLayout descriptor_set_layout = nullptr;
    std::vector<vk::DescriptorSet> descriptor_sets;
};

template <typename T>
constexpr vk::PushConstantRange PushConstant(vk::ShaderStageFlags stages) {
    static_assert(Helpers::VerifyLayoutStd140<T>());
    return {
        .stageFlags = stages,
        .offset = 0,
        .size = sizeof(T),
    };
}

} // namespace Renderer
