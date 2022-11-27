// Copyright 2022 Pengfei Zhu
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <memory>
#include <vector>

namespace Scene {

class Object;

class Scene {
public:
private:
    std::vector<std::unique_ptr<Object>> objects;
};

} // namespace Scene
