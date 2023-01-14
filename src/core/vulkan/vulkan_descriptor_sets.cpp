// Copyright 2023 Pengfei Zhu
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <map>
#include "common/ranges.h"
#include "core/vulkan/vulkan_descriptor_sets.h"
#include "core/vulkan/vulkan_device.h"

namespace Renderer {

VulkanDescriptorSets::VulkanDescriptorSets(const VulkanDevice& device_, std::size_t count_,
                                           const vk::ArrayProxy<const DescriptorBinding>& bindings)
    : device(device_), count(count_) {

    binding_info.reserve(bindings.size());
    for (u32 i = 0; i < bindings.size(); ++i) {
        const auto& binding = *(bindings.begin() + i);
        binding_info.emplace_back(vk::DescriptorSetLayoutBinding{
            .binding = i,
            .descriptorType = binding.type,
            .descriptorCount = binding.array_size,
            .stageFlags = binding.stages,
        });
    }

    std::map<vk::DescriptorType, u32> descriptor_type_count;
    for (const auto& binding : bindings) {
        descriptor_type_count[binding.type] += static_cast<u32>(binding.array_size * count);
    }
    descriptor_pool = vk::raii::DescriptorPool{
        *device,
        {
            .flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet,
            .maxSets = static_cast<u32>(count),
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

    descriptor_set_layout =
        vk::raii::DescriptorSetLayout{*device,
                                      {
                                          .bindingCount = static_cast<u32>(binding_info.size()),
                                          .pBindings = binding_info.data(),
                                      }};

    descriptor_sets.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        // Note: We do not use RAII here as it would require FreeDescriptorSet on the pool
        descriptor_sets.emplace_back((**device)
                                         .allocateDescriptorSets({
                                             .descriptorPool = *descriptor_pool,
                                             .descriptorSetCount = 1,
                                             .pSetLayouts = &*descriptor_set_layout,
                                         })
                                         .at(0));
    }
    for (u32 i = 0; i < bindings.size(); ++i) {
        const auto& binding = *(bindings.begin() + i);
        UpdateDescriptor(i, binding.value);
    }
}

VulkanDescriptorSets::~VulkanDescriptorSets() = default;

void VulkanDescriptorSets::UpdateDescriptor(
    std::size_t binding_idx, const DescriptorBinding::DescriptorBindingValue& binding_value) {

    if (const auto* values = std::get_if<DescriptorBinding::BuffersValue>(&binding_value)) {
        for (std::size_t i = 0; i < count; ++i) {
            const auto& value = values->size() > i ? *(values->begin() + i) : (*values->begin());
            const auto& buffers = value.buffers;
            device->updateDescriptorSets(
                {{
                    .dstSet = descriptor_sets[i],
                    .dstBinding = static_cast<u32>(binding_idx),
                    .descriptorCount = static_cast<u32>(buffers.size()),
                    .descriptorType = binding_info[binding_idx].descriptorType,
                    .pBufferInfo = Common::VectorFromRange(
                                       buffers | std::views::transform([](vk::Buffer buffer) {
                                           return vk::DescriptorBufferInfo{
                                               .buffer = buffer,
                                               .offset = 0,
                                               .range = VK_WHOLE_SIZE,
                                           };
                                       }))
                                       .data(),
                }},
                {});
        }
    }
    if (const auto* values =
            std::get_if<DescriptorBinding::CombinedImageSamplersValue>(&binding_value)) {
        for (std::size_t i = 0; i < count; ++i) {
            const auto& value = values->size() > i ? *(values->begin() + i) : (*values->begin());
            const auto& images = value.images;
            device->updateDescriptorSets(
                {{
                    .dstSet = descriptor_sets[i],
                    .dstBinding = static_cast<u32>(binding_idx),
                    .descriptorCount = static_cast<u32>(images.size()),
                    .descriptorType = binding_info[binding_idx].descriptorType,
                    .pImageInfo = Common::VectorFromRange(
                                      images | std::views::transform([this](const auto& image) {
                                          return vk::DescriptorImageInfo{
                                              .sampler = image.sampler ? image.sampler
                                                                       : *device.default_sampler,
                                              .imageView = image.image,
                                              .imageLayout = image.layout,
                                          };
                                      }))
                                      .data(),
                }},
                {});
        }
    }
    if (const auto* values = std::get_if<DescriptorBinding::AccelStructuresValue>(&binding_value)) {
        for (std::size_t i = 0; i < count; ++i) {
            const auto& value = values->size() > i ? *(values->begin() + i) : (*values->begin());
            const auto& accel_structures = value.accel_structures;
            device->updateDescriptorSets(
                {Helpers::GenericStructureChain{
                    vk::WriteDescriptorSet{
                        .dstSet = descriptor_sets[i],
                        .dstBinding = static_cast<u32>(binding_idx),
                        .descriptorCount = static_cast<u32>(accel_structures.size()),
                        .descriptorType = binding_info[binding_idx].descriptorType,
                    },
                    vk::WriteDescriptorSetAccelerationStructureKHR{
                        .accelerationStructureCount = static_cast<u32>(accel_structures.size()),
                        .pAccelerationStructures = accel_structures.data(),
                    },
                }},
                {});
        }
    }
}

} // namespace Renderer
