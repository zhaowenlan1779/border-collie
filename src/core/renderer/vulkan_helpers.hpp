// Copyright 2022 Pengfei Zhu
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <memory>
#include <utility>
#include <boost/pfr.hpp>
#include <glm/glm.hpp>
#include <vulkan/vulkan_raii.hpp>
#include "common/common_types.h"

// RAII helpers

struct CommandBufferContext {
    explicit CommandBufferContext(vk::raii::CommandBuffer& command_buffer_,
                                  const vk::CommandBufferBeginInfo& begin_info)
        : command_buffer(command_buffer_) {

        command_buffer.begin(begin_info);
    }

    ~CommandBufferContext() {
        command_buffer.end();
    }

    vk::raii::CommandBuffer& command_buffer;
};

struct CommandBufferRenderPassContext {
    explicit CommandBufferRenderPassContext(vk::raii::CommandBuffer& command_buffer_,
                                            const vk::RenderPassBeginInfo& begin_info,
                                            vk::SubpassContents contents)
        : command_buffer(command_buffer_) {

        command_buffer.beginRenderPass(begin_info, contents);
    }

    ~CommandBufferRenderPassContext() {
        command_buffer.endRenderPass();
    }

    vk::raii::CommandBuffer& command_buffer;
};

// Attributes helpers

namespace detail {

template <typename T>
constexpr vk::Format FormatOf = vk::Format::eUndefined;
template <>
constexpr vk::Format FormatOf<float> = vk::Format::eR32Sfloat;
template <>
constexpr vk::Format FormatOf<glm::vec2> = vk::Format::eR32G32Sfloat;
template <>
constexpr vk::Format FormatOf<glm::vec3> = vk::Format::eR32G32B32Sfloat;
template <>
constexpr vk::Format FormatOf<glm::vec4> = vk::Format::eR32G32B32A32Sfloat;
template <>
constexpr vk::Format FormatOf<s32> = vk::Format::eR32Sint;
template <>
constexpr vk::Format FormatOf<glm::ivec2> = vk::Format::eR32G32Sint;
template <>
constexpr vk::Format FormatOf<glm::ivec3> = vk::Format::eR32G32B32Sint;
template <>
constexpr vk::Format FormatOf<glm::ivec4> = vk::Format::eR32G32B32A32Sint;
template <>
constexpr vk::Format FormatOf<u32> = vk::Format::eR32Uint;
template <>
constexpr vk::Format FormatOf<glm::uvec2> = vk::Format::eR32G32Uint;
template <>
constexpr vk::Format FormatOf<glm::uvec3> = vk::Format::eR32G32B32Uint;
template <>
constexpr vk::Format FormatOf<glm::uvec4> = vk::Format::eR32G32B32A32Uint;

template <typename T, u32 TupleIdx>
consteval std::size_t OffsetOf() {
    union {
        char c[sizeof(T)];
        T o;
    } u;
    for (std::size_t i = 0; i < sizeof(T); ++i) {
        // Not exactly sure about the legality of taking address of inactive union member,
        // but all of GCC/Clang/MSVC admit this
        if (static_cast<const void*>(&u.c[i]) ==
            static_cast<const void*>(std::addressof(boost::pfr::get<TupleIdx>(u.o)))) {
            return i;
        }
    }
    throw "Fail";
}

template <typename T, u32 BindingIdx, u32 LocationStart, u32 TupleIdx>
consteval vk::VertexInputAttributeDescription AttributeDescriptionFor() {
    static_assert(FormatOf<boost::pfr::tuple_element_t<TupleIdx, T>> != vk::Format::eUndefined,
                  "Data format not supported");
    return {
        .location = TupleIdx + LocationStart,
        .binding = BindingIdx,
        .format = FormatOf<boost::pfr::tuple_element_t<TupleIdx, T>>,
        .offset = static_cast<u32>(OffsetOf<T, TupleIdx>()),
    };
}

template <typename T, u32 BindingIdx, u32 LocationStart, std::size_t... Idxs>
consteval std::array<vk::VertexInputAttributeDescription, sizeof...(Idxs)>
AttributeDescriptionsHelper(std::index_sequence<Idxs...>) {
    return {{AttributeDescriptionFor<T, BindingIdx, LocationStart, Idxs>()...}};
}

} // namespace detail

template <typename T, u32 BindingIdx = 0, u32 LocationStart = 0>
consteval std::array<vk::VertexInputAttributeDescription, boost::pfr::tuple_size_v<T>>
AttributeDescriptionsFor() {
    return detail::AttributeDescriptionsHelper<T, BindingIdx, LocationStart>(
        std::make_index_sequence<boost::pfr::tuple_size_v<T>>());
}

namespace detail::test {
struct Vertex {
    glm::vec2 pos;
    glm::vec3 color;
};

// Small testbench
static_assert(AttributeDescriptionsFor<Vertex>() ==
              std::array<vk::VertexInputAttributeDescription, 2>{{
                  {
                      .location = 0,
                      .binding = 0,
                      .format = vk::Format::eR32G32Sfloat,
                      .offset = offsetof(Vertex, pos),
                  },
                  {
                      .location = 1,
                      .binding = 0,
                      .format = vk::Format::eR32G32B32Sfloat,
                      .offset = offsetof(Vertex, color),
                  },
              }});
} // namespace detail::test
