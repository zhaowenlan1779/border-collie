// Copyright 2022 Pengfei Zhu
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <memory>
#include <tuple>
#include <type_traits>
#include <utility>
#include <boost/pfr.hpp>
#include <glm/glm.hpp>
#include <vulkan/vulkan_raii.hpp>
#include "common/alignment.h"
#include "common/common_types.h"
#include "common/temp_ptr.h"
#include "core/vulkan/vulkan_device.h"

namespace Renderer {
class VulkanImage;
}

namespace Renderer::Helpers {

// RAII helpers

/// Begins/ends a command buffer, does not submit anything.
struct CommandBufferContext {
    explicit CommandBufferContext(const vk::raii::CommandBuffer& command_buffer_,
                                  const vk::CommandBufferBeginInfo& begin_info)
        : command_buffer(command_buffer_) {

        command_buffer.begin(begin_info);
    }

    ~CommandBufferContext() {
        command_buffer.end();
    }

    const vk::raii::CommandBuffer& command_buffer;
};

/// Allocates a command buffer, begins/ends it, and submits it to a queue.
struct OneTimeCommandContext {
    explicit OneTimeCommandContext(const VulkanDevice& device_)
        : device(device_), command_buffers{*device,
                                           {
                                               .commandPool = *device.command_pool,
                                               .level = vk::CommandBufferLevel::ePrimary,
                                               .commandBufferCount = 1,
                                           }} {

        command_buffers[0].begin({.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit});
    }

    ~OneTimeCommandContext() {
        command_buffers[0].end();
        device.graphics_queue.submit({{
            .commandBufferCount = 1,
            .pCommandBuffers = TempArr<vk::CommandBuffer>{*command_buffers[0]},
        }});
        device.graphics_queue.waitIdle();
    }

    vk::raii::CommandBuffer& operator*() {
        return command_buffers[0];
    }

    const VulkanDevice& device;
    vk::raii::CommandBuffers command_buffers;
};

struct CommandBufferRenderPassContext {
    explicit CommandBufferRenderPassContext(const vk::raii::CommandBuffer& command_buffer_,
                                            const vk::RenderPassBeginInfo& begin_info,
                                            vk::SubpassContents contents)
        : command_buffer(command_buffer_) {

        command_buffer.beginRenderPass(begin_info, contents);
    }

    ~CommandBufferRenderPassContext() {
        command_buffer.endRenderPass();
    }

    const vk::raii::CommandBuffer& command_buffer;
};

// Structure chain that only constrains the type of the first element
template <typename T>
struct GenericStructureChain {
    template <typename... Args>
    GenericStructureChain(T&& t, Args&&... args) {
        using TupleType = vk::StructureChain<std::remove_cvref_t<T>, std::remove_cvref_t<Args>...>;
        static_assert(std::is_trivially_destructible_v<TupleType>);

        data = std::make_unique<u8[]>(sizeof(TupleType));
        auto* tuple = new (data.get()) TupleType{std::forward<T>(t), std::forward<Args>(args)...};
        first_ptr = &std::get<0>(*tuple);
    }

    operator T*() {
        return first_ptr;
    }
    operator const T*() const {
        return first_ptr;
    }
    operator T&() {
        return *first_ptr;
    }
    operator const T&() const {
        return *first_ptr;
    }

    std::unique_ptr<u8[]> data;
    T* first_ptr{};
};

void ImageLayoutTransition(const vk::raii::CommandBuffer& command_buffer,
                           const std::unique_ptr<VulkanImage>& image,
                           vk::ImageMemoryBarrier2 params);

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

// std140 Alignment checker

namespace detail {

// Scalars
// More like "is GLSL-usable scalar"
template <typename T>
constexpr bool IsScalar = false;
template <>
constexpr bool IsScalar<bool> = true;
template <>
constexpr bool IsScalar<float> = true;
template <>
constexpr bool IsScalar<double> = true;
template <>
constexpr bool IsScalar<s32> = true;
template <>
constexpr bool IsScalar<u32> = true;

struct Alignment {
    std::size_t Alignment;
    std::size_t Size;
};

template <typename T>
consteval Alignment CalculateAlignment(T);

// Vectors
template <glm::length_t L, typename T, glm::qualifier Q>
consteval Alignment CalculateAlignment(glm::vec<L, T, Q>) {
    const auto& ScalarAlignment = CalculateAlignment(T{});
    if constexpr (L == 2) {
        return {ScalarAlignment.Alignment * 2, ScalarAlignment.Size * 2};
    } else {
        return {ScalarAlignment.Alignment * 4, ScalarAlignment.Size * 4};
    }
}

// Arrays
template <typename T>
consteval Alignment CalculateAlignment(T[]) {
    throw "Raw arrays are prohibited because boost::pfr does not support them";
    return {};
}

template <typename T, std::size_t N>
consteval Alignment CalculateAlignment(std::array<T, N>) {
    const std::size_t ElementAlignment = Common::AlignUp(CalculateAlignment(T{}).Alignment, 16);
    static_assert(Common::AlignUp(sizeof(T), alignof(T)) % ElementAlignment == 0,
                  "Element type does not satisfy array stride requirements");

    return {
        .Alignment = ElementAlignment,
        .Size = ElementAlignment * N,
    };
}

// Matrices
template <glm::length_t C, glm::length_t R, typename T, glm::qualifier Q>
consteval Alignment CalculateAlignment(glm::mat<C, R, T, Q>) {
    return CalculateAlignment(std::array<glm::vec<R, T, Q>, C>{});
}

// Structures
template <typename T, std::size_t... Idxs>
consteval Alignment StructureAlignmentImpl(std::index_sequence<Idxs...>) {
    return {
        .Alignment = Common::AlignUp(
            std::max({CalculateAlignment(boost::pfr::tuple_element_t<Idxs, T>{}).Alignment...}),
            16),
        .Size = Common::AlignUp(
            (CalculateAlignment(boost::pfr::tuple_element_t<Idxs, T>{}).Size + ...), 16),
    };
}

template <typename T>
consteval Alignment CalculateAlignment(T) {
    if constexpr (IsScalar<T>) {
        return {sizeof(T), sizeof(T)};
    }
    if constexpr (!std::is_aggregate_v<T>) {
        throw "Unsupported type!";
    }
    static_assert(boost::pfr::tuple_size_v<T> != 0, "Empty structures are not supported");
    return StructureAlignmentImpl<T>(std::make_index_sequence<boost::pfr::tuple_size_v<T>>());
}

template <typename T>
consteval bool VerifyAlignmentSingle(T);

template <glm::length_t L, typename T, glm::qualifier Q>
consteval bool VerifyAlignmentSingle(glm::vec<L, T, Q>) {
    return true;
}

template <typename T>
consteval bool VerifyAlignmentSingle(T[]) {
    throw "Raw arrays are prohibited because boost::pfr does not support them";
    return false;
}

template <typename T, std::size_t N>
consteval bool VerifyAlignmentSingle(std::array<T, N>) {
    return VerifyAlignmentSingle(T{});
}

template <glm::length_t C, glm::length_t R, typename T, glm::qualifier Q>
consteval bool VerifyAlignmentSingle(glm::mat<C, R, T, Q>) {
    return true;
}

// Verifies alignment requirements for a structure and its members.
// Does not verify sub-structures.
template <typename T, std::size_t... Idxs>
consteval bool VerifyAlignmentSingleStructureImpl(std::index_sequence<Idxs...>) {
    const std::array<Alignment, sizeof...(Idxs)>& Alignments = {
        CalculateAlignment(boost::pfr::tuple_element_t<Idxs, T>{})...};
    const std::array<std::size_t, sizeof...(Idxs)>& Offsets = {OffsetOf<T, Idxs>()...};

    std::size_t expected_offset = 0;
    for (std::size_t i = 0; i < sizeof...(Idxs); ++i) {
        expected_offset = Common::AlignUp(expected_offset, Alignments[i].Alignment);
        if (Offsets[i] != expected_offset) {
            return false;
        }
        expected_offset += Alignments[i].Size;
    }
    return expected_offset == sizeof(T);
}

template <typename T>
consteval bool VerifyAlignmentSingle(T) {
    if constexpr (IsScalar<T>) {
        return true;
    }
    return VerifyAlignmentSingleStructureImpl<T>(
        std::make_index_sequence<boost::pfr::tuple_size_v<T>>());
}

template <typename T>
consteval bool VerifyAlignmentImpl(T);

template <glm::length_t L, typename T, glm::qualifier Q>
consteval bool VerifyAlignmentImpl(glm::vec<L, T, Q>) {
    return true;
}

template <typename T>
consteval bool VerifyAlignmentImpl(T[]) {
    throw "Raw arrays are prohibited because boost::pfr does not support them";
    return false;
}

template <typename T, std::size_t N>
consteval bool VerifyAlignmentImpl(std::array<T, N> a) {
    CalculateAlignment(a); // Ensure stride
    return VerifyAlignmentSingle(T{});
}

template <glm::length_t C, glm::length_t R, typename T, glm::qualifier Q>
consteval bool VerifyAlignmentImpl(glm::mat<C, R, T, Q>) {
    return true;
}

// Verifies alignment requirements for the structure itself and sub-structures.
template <typename T, std::size_t... Idxs>
consteval bool VerifyAlignmentStructureImpl(std::index_sequence<Idxs...>) {
    return VerifyAlignmentSingle(T{}) &&
           (... && VerifyAlignmentSingle(boost::pfr::tuple_element_t<Idxs, T>{}));
}

template <typename T>
consteval bool VerifyAlignmentImpl(T) {
    return VerifyAlignmentStructureImpl<T>(std::make_index_sequence<boost::pfr::tuple_size_v<T>>());
}

} // namespace detail

template <typename T>
consteval bool VerifyLayoutStd140() {
    return detail::VerifyAlignmentImpl(T{});
}

} // namespace Renderer::Helpers
