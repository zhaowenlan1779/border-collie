// Copyright 2022 Pengfei Zhu
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <memory>
#include <utility>
#include <variant>

namespace Scene {

struct Triangle {
    std::size_t v0;
    std::size_t v1;
    std::size_t v2;
};

using Primitive = std::variant<std::unique_ptr<Triangle>>;

class Material;
using Renderable = std::pair<Primitive, std::shared_ptr<Material>>;

} // namespace Scene
