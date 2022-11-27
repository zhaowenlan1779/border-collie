// Copyright 2022 Pengfei Zhu
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <vector>
#include "core/scene/primitive.h"

namespace Scene {

class Object {
public:
    virtual ~Object() = 0;
    virtual const std::vector<Renderable>& Renderables();
    virtual const std::vector<glm::vec3>& Vertices();
};

} // namespace Scene
