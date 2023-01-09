// Copyright 2023 Pengfei Zhu
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <algorithm>
#include <optional>
#include <string_view>
#include <type_traits>
#include <vector>
#include <boost/pfr.hpp>
#include "common/common_types.h"
#include "core/gltf/simdjson.h"

namespace JSON {

template <std::size_t N>
struct StringLiteral {
    constexpr StringLiteral(const char (&str)[N]) {
        std::copy_n(str, N, value);
    }

    constexpr operator std::string_view() const {
        return std::string_view{value, value + N};
    }

    char value[N];
};

namespace detail {

template <typename T>
using NumberType = std::conditional_t<std::is_floating_point_v<T>, double,
                                      std::conditional_t<std::is_signed_v<T>, s64, u64>>;

template <typename T>
void DeserializeStruct(T& out, simdjson::simdjson_result<simdjson::ondemand::value> json);

template <typename T>
    requires(std::is_scalar_v<T>) bool
DeserializeValue(T& out, simdjson::simdjson_result<simdjson::ondemand::value> json) {
    detail::NumberType<T> raw_value{};
    if (json.get(raw_value)) {
        return false;
    } else {
        out = static_cast<T>(raw_value);
        return true;
    }
}

template <typename T>
    requires(!std::is_scalar_v<T>) bool
DeserializeValue(T& out, simdjson::simdjson_result<simdjson::ondemand::value> json) {
    if (json.error()) {
        return false;
    } else {
        DeserializeStruct(out, std::move(json));
        return true;
    }
}

template <>
bool DeserializeValue(std::string_view& out,
                      simdjson::simdjson_result<simdjson::ondemand::value> json) {
    return !json.get(out);
}

template <glm::length_t L, typename T, glm::qualifier Q>
bool DeserializeValue(glm::vec<L, T, Q>& out,
                      simdjson::simdjson_result<simdjson::ondemand::value> json) {
    // Load as array
    simdjson::ondemand::array array;
    if (json.get(array)) {
        return false;
    }

    if (array.count_elements().value() != L) {
        SPDLOG_ERROR("GLM array is of incorrect size");
        throw std::runtime_error("GLM array is of incorrect size");
    }

    std::size_t i = 0;
    for (detail::NumberType<T> element : array) {
        out[i] = static_cast<T>(element);
        ++i;
    }
    return true;
}

template <glm::length_t C, glm::length_t R, typename T, glm::qualifier Q>
bool DeserializeValue(glm::mat<C, R, T, Q>& out,
                      simdjson::simdjson_result<simdjson::ondemand::value> json) {
    // Load as array
    simdjson::ondemand::array array;
    if (json.get(array)) {
        return false;
    }

    if (array.count_elements().value() != C * R) {
        SPDLOG_ERROR("GLM array is of incorrect size");
        throw std::runtime_error("GLM array is of incorrect size");
    }

    // Column major
    std::size_t i = 0, j = 0;
    for (detail::NumberType<T> element : array) {
        out[i][j] = static_cast<T>(element);
        ++j;
        if (j == R) {
            j = 0;
            ++i;
        }
    }
    return true;
}

} // namespace detail

template <typename T, StringLiteral Name, auto DefaultValue = std::nullopt>
struct Field;

template <typename T, StringLiteral Name, auto DefaultValue>
    requires(std::is_scalar_v<T>)
struct Field<T, Name, DefaultValue> {
    T value;

    operator T() const {
        return value;
    }
    operator T&() {
        return value;
    }
};

template <typename T, StringLiteral Name, auto DefaultValue>
    requires(!std::is_scalar_v<T>)
struct Field<T, Name, DefaultValue> : T {};

template <typename T, StringLiteral Name>
struct Field<T, Name, std::nullopt> : std::optional<T> {};

namespace detail {

template <typename T, StringLiteral Name, auto DefaultValue>
void DeserializeField(Field<T, Name, DefaultValue>& out,
                      simdjson::simdjson_result<simdjson::ondemand::value> parent_obj) {
    if (!DeserializeValue(static_cast<T&>(out), parent_obj[Name.value])) {
        static_cast<T&>(out) = DefaultValue;
    }
}

template <typename T, StringLiteral Name>
void DeserializeField(Field<T, Name, std::nullopt>& out,
                      simdjson::simdjson_result<simdjson::ondemand::value> parent_obj) {
    T object{};
    if (DeserializeValue(object, parent_obj[Name.value])) {
        out.std::optional<T>::operator=(std::move(object));
    } else {
        out.std::optional<T>::operator=(std::nullopt);
    }
}

} // namespace detail

// DefaultValue does not change the structure so long as it's not std::nullopt
template <typename T, StringLiteral Name>
struct RequiredField : Field<T, Name, nullptr> {};

namespace detail {

template <typename T, StringLiteral Name>
void DeserializeField(RequiredField<T, Name>& out,
                      simdjson::simdjson_result<simdjson::ondemand::value> parent_obj) {
    if (!DeserializeValue(static_cast<T&>(out), parent_obj[Name.value])) {
        SPDLOG_CRITICAL("Could not deserialize required field {}", std::string_view{Name});
        throw std::runtime_error("Could not deserialize a required field");
    }
}

} // namespace detail

template <typename T, StringLiteral Name>
struct Array : std::vector<T> {};

namespace detail {

template <typename T, StringLiteral Name>
void DeserializeField(Array<T, Name>& out,
                      simdjson::simdjson_result<simdjson::ondemand::value> parent_obj) {
    out.clear();

    auto sub_obj = parent_obj[Name.value];
    if (!sub_obj.error()) {
        for (auto obj : sub_obj.get_array()) {
            out.emplace_back();
            DeserializeValue(out.back(), std::move(obj));
        }
    }
}

} // namespace detail

namespace detail {

template <typename T, std::size_t... Idxs>
void DeserializeStructImpl(T& out, simdjson::simdjson_result<simdjson::ondemand::value> json,
                           std::index_sequence<Idxs...>) {
    (..., DeserializeField(boost::pfr::get<Idxs, T>(out), json));
}

template <typename T>
void DeserializeStruct(T& out, simdjson::simdjson_result<simdjson::ondemand::value> json) {
    DeserializeStructImpl(out, std::move(json),
                          std::make_index_sequence<boost::pfr::tuple_size_v<T>>());
}

} // namespace detail

template <typename T>
T Deserialize(simdjson::simdjson_result<simdjson::ondemand::value> json) {
    T out{};
    detail::DeserializeValue(out, std::move(json));
    return out;
}

} // namespace JSON
