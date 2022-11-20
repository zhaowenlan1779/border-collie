// Copyright 2022 Pengfei Zhu
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <array>

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
