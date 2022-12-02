// Copyright 2022 Pengfei Zhu
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>
#include "common/log.h"

namespace Common {
void InitializeLogging() {
    auto* sink = spdlog::default_logger()->sinks().front().get();
    if (auto* color_sink = dynamic_cast<spdlog::sinks::stdout_color_sink_mt*>(sink)) {
#ifdef _WIN32
        color_sink->set_color(spdlog::level::info, 0xF); // white
#else
        color_sink->set_color(spdlog::level::info, color_sink->white);
#endif
    }

    spdlog::set_pattern("%^[%T.%e] [%l] %@:%! %v%$");
}
} // namespace Common
