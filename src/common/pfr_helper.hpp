// Copyright 2023 Pengfei Zhu
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <array>
#include <type_traits>
#include <utility>
#include <boost/pfr.hpp>

namespace Common::PFR {

namespace detail {

template <typename U, typename T, std::size_t... Idxs>
consteval std::size_t GetIndexImpl(std::index_sequence<Idxs...>) {
    constexpr std::array<bool, sizeof...(Idxs)> List{
        std::is_same_v<decltype(boost::pfr::get<Idxs, T>(std::declval<T>())), U>...};
    for (std::size_t i = 0; i < List.size(); ++i) {
        if (List[i]) {
            return i;
        }
    }
    throw "No index found";
}

template <typename U, typename T>
consteval std::size_t GetIndex() {
    return GetIndexImpl<U, T>(std::make_index_sequence<boost::pfr::tuple_size_v<T>>());
}

template <typename U, typename T, std::size_t... Idxs>
consteval std::size_t GetIndexDerivedImpl(std::index_sequence<Idxs...>) {
    constexpr std::array<bool, sizeof...(Idxs)> List{
        std::is_base_of_v<U, decltype(boost::pfr::get<Idxs, T>(std::declval<T>()))>...};
    for (std::size_t i = 0; i < List.size(); ++i) {
        if (List[i]) {
            return i;
        }
    }
    throw "No index found";
}

template <typename U, typename T>
consteval std::size_t GetIndexDerived() {
    return GetIndexDerivedImpl<U, T>(std::make_index_sequence<boost::pfr::tuple_size_v<T>>());
}

} // namespace detail

template <typename U, typename T>
constexpr U& Get(T& obj) {
    return boost::pfr::get<detail::GetIndex<U, T>()>(obj);
}

template <typename U, typename T>
constexpr const U& Get(const T& obj) {
    return boost::pfr::get<detail::GetIndex<U, T>()>(obj);
}

template <typename U, typename T>
constexpr U& GetDerived(T& obj) {
    return boost::pfr::get<detail::GetIndexDerived<U, T>()>(obj);
}

template <typename U, typename T>
constexpr const U& GetDerived(const T& obj) {
    return boost::pfr::get<detail::GetIndexDerived<U, T>()>(obj);
}

} // namespace Common::PFR
