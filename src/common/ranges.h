// Copyright 2022 Pengfei Zhu
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <ranges>
#include <vector>

namespace Common {

template <typename Container, std::ranges::common_range Range>
Container FromRange(Range&& r) {
    return Container{begin(r), end(r)};
}

template <std::ranges::range Range>
auto VectorFromRange(Range&& r) {
    using ValueType = std::ranges::range_value_t<Range>;
    return FromRange<std::vector<ValueType>>(r);
}

} // namespace Common
