// Copyright 2022 Pengfei Zhu
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <memory>
#include <utility>
#include <glm/glm.hpp>
#include <vulkan/vulkan_raii.hpp>
#include "common/common_types.h"

namespace Renderer {

class VulkanBuffer;
class VulkanImmUploadBuffer;
class VulkanDevice;

class VulkanAccelStructureMemory : NonCopyable {
public:
    explicit VulkanAccelStructureMemory(const VulkanDevice& device,
                                        vk::AccelerationStructureCreateInfoKHR create_info);
    ~VulkanAccelStructureMemory();

    vk::AccelerationStructureKHR operator*() const noexcept {
        return *as;
    }

    std::unique_ptr<VulkanBuffer> buffer;

private:
    vk::raii::AccelerationStructureKHR as = nullptr;
};

// Generic acceleration structure
class VulkanAccelStructure {
public:
    // (Typically) bottom level
    explicit VulkanAccelStructure(
        VulkanDevice& device,
        const vk::ArrayProxy<const vk::AccelerationStructureGeometryKHR>& geometries,
        const vk::ArrayProxy<const vk::AccelerationStructureBuildRangeInfoKHR>& build_ranges,
        vk::AccelerationStructureTypeKHR type = vk::AccelerationStructureTypeKHR::eBottomLevel);

    // Top level
    struct BLASInstance {
        const VulkanAccelStructure& blas;
        glm::mat4 transform;
        u32 custom_index{};
    };
    explicit VulkanAccelStructure(const vk::ArrayProxy<const BLASInstance>& instances);

    ~VulkanAccelStructure();

    void Compact();
    void Cleanup();

    vk::AccelerationStructureKHR operator*() const noexcept {
        return **compacted_as;
    }

private:
    void Init(const vk::ArrayProxy<const vk::AccelerationStructureGeometryKHR>& geometries,
              const vk::ArrayProxy<const vk::AccelerationStructureBuildRangeInfoKHR>& build_ranges);

    VulkanDevice& device;
    vk::AccelerationStructureTypeKHR type;
    std::unique_ptr<VulkanBuffer> scratch_buffer{};
    std::unique_ptr<VulkanAccelStructureMemory> as{};
    std::unique_ptr<VulkanAccelStructureMemory> compacted_as{};
    vk::raii::CommandBuffer build_cmdbuf = nullptr;
    vk::raii::CommandBuffer compact_cmdbuf = nullptr;
    vk::raii::Fence build_fence = nullptr;
    vk::raii::Fence compact_fence = nullptr;
    vk::raii::QueryPool query_pool = nullptr;

    // Really means 'whether compact has started'
    bool compacted = false;

    // For top level initialization
    std::unique_ptr<VulkanImmUploadBuffer> instances_buffer{};

    friend class VulkanPathTracerHW;
};

} // namespace Renderer
