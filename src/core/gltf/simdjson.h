// Copyright 2023 Pengfei Zhu
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

// Wrapper to disable certain warnings in MSVC

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4100 4706)
#endif

#include <simdjson.h>

#ifdef _MSC_VER
#pragma warning(pop)
#endif
