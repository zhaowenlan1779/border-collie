// Copyright 2022 Pengfei Zhu
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <filesystem>
#include <vector>
#include "common/common_types.h"

namespace Common {

std::vector<u8> ReadFileContents(const std::filesystem::path& path);

}
