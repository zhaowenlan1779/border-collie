// Copyright 2022 Pengfei Zhu
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <spdlog/spdlog.h>
#include "common/log.h"

namespace Common {
void InitializeLogging() {
    spdlog::set_pattern("[%T.%e] [%l] %@:%! %v");
}
} // namespace Common
