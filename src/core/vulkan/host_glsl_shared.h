// Copyright 2023 Pengfei Zhu
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#ifndef HOST_GLSL_SHARED_H
#define HOST_GLSL_SHARED_H

// Helper for defining structures used in both C++ and GLSL

#ifdef GL_core_profile

#define BEGIN_STRUCT(Name) struct Name {

#define END_STRUCT(Name)                                                                           \
    }                                                                                              \
    ;

#define INSERT_PADDING(num_scalars)

#else

#include <glm/glm.hpp>
#include "common/common_types.h"
#include "core/vulkan/vulkan_helpers.hpp"

static_assert(sizeof(int) == 4);

#define BEGIN_STRUCT(Name)                                                                         \
    namespace Renderer::GLSL {                                                                     \
    using namespace glm;                                                                           \
    using uint = u32;                                                                              \
                                                                                                   \
    struct Name {

#define END_STRUCT(Name)                                                                           \
    }                                                                                              \
    ;                                                                                              \
    struct Name##Block {                                                                           \
        Name obj;                                                                                  \
    };                                                                                             \
    static_assert(Helpers::VerifyLayoutStd140<Name>() &&                                           \
                  Helpers::VerifyLayoutStd140<Name##Block>());                                     \
    }

/// Textually concatenates two tokens. The double-expansion is required by the C preprocessor.
#define CONCAT2(x, y) DO_CONCAT2(x, y)
#define DO_CONCAT2(x, y) x##y
#define INSERT_PADDING_1 float CONCAT2(pad, __LINE__);
#define INSERT_PADDING_2                                                                           \
    float CONCAT2(pad, __LINE__);                                                                  \
    float CONCAT2(pad2_, __LINE__);
#define INSERT_PADDING_3                                                                           \
    float CONCAT2(pad, __LINE__);                                                                  \
    float CONCAT2(pad2_, __LINE__);                                                                \
    float CONCAT2(pad3_, __LINE__);
#define INSERT_PADDING(num_scalars) INSERT_PADDING_##num_scalars

#endif

#endif
