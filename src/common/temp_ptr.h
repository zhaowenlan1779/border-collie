// Copyright 2022 Pengfei Zhu
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <algorithm>
#include <array>
#include <initializer_list>
#include <limits>
#include <type_traits>
#include <utility>
#include <boost/pfr.hpp>

/**
 * Helper for passing pointers to temporary objects.
 */
template <typename T>
class TempPtr {
public:
    TempPtr() {}
    TempPtr(T&& t) : temporary(t) {}

    operator const T*() const {
        return &temporary;
    }

private:
    const T temporary;
};

/**
 * Helper for passing pointers to array of temporary objects.
 */
template <typename T>
class TempArr {
public:
    TempArr() {}
    TempArr(std::initializer_list<T> t) : temporary(std::move(t)) {}

    operator const T*() const {
        return temporary.begin();
    }

private:
    const std::initializer_list<T> temporary;
};

namespace detail {

template <typename T>
struct MemberPointerTraits {};

template <typename T, typename U>
struct MemberPointerTraits<U T::*> {
    using Class = T;
    using ValueType = U;
};

} // namespace detail

template <auto Ptr, auto Count>
struct ArrSpec {};

namespace detail {

template <typename... Ts>
struct TempWrapperImpl {
    using Class = void;
};

template <auto Ptr, auto Count, typename... As>
struct TempWrapperImpl<ArrSpec<Ptr, Count>, As...> {
    using Class = typename MemberPointerTraits<decltype(Ptr)>::Class;
    static_assert(std::is_same_v<Class, typename MemberPointerTraits<decltype(Count)>::Class>);
    static_assert(sizeof...(As) == 0 ||
                  std::is_same_v<Class, typename TempWrapperImpl<As...>::Class>);

    constexpr void Save(Class& obj) {
        array.assign(obj.*Ptr, obj.*Ptr + obj.*Count);
        if constexpr (sizeof...(As) != 0) {
            parent.Save(obj);
        }
    }

    constexpr void Unwrap(Class& obj) const {
        obj.*Ptr = array.data();
        if constexpr (sizeof...(As) != 0) {
            parent.Unwrap(obj);
        }
    }

    std::vector<std::remove_cv_t<
        std::remove_pointer_t<typename MemberPointerTraits<decltype(Ptr)>::ValueType>>>
        array;
    TempWrapperImpl<As...> parent;
};

} // namespace detail

/**
 * An object that wraps a temporary, and a temporary array referenced by it.
 */
template <typename... ArrSpecs>
class TempWrapper {
    using T = typename detail::TempWrapperImpl<ArrSpecs...>::Class;

public:
    using ValueType = T;

    TempWrapper(T&& t) : obj(std::move(t)) {
        impl.Save(obj);
    }

    T Unwrap() const {
        T result = obj;
        impl.Unwrap(result);
        return result;
    }

private:
    T obj;
    detail::TempWrapperImpl<ArrSpecs...> impl;
};

// Helper for 'From Range' operations
// This is necessary because we need the wrapper vector to be part of the original full expression
#define WRAPPED_RANGE(r)                                                                           \
    Common::VectorFromRange(                                                                       \
        Common::VectorFromRange((r)) |                                                             \
        std::views::transform([](const auto& wrapper) { return wrapper.Unwrap(); }))
