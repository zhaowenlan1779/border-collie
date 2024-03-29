// Copyright 2013 Dolphin Emulator Project / 2014 Citra Emulator Project
//           2023 Pengfei Zhu
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <cstdlib>
#include <spdlog/spdlog.h>

#ifndef _MSC_VER

#ifdef ARCHITECTURE_x86_64
#define Crash() __asm__ __volatile__("int $3")
#else
#define Crash() exit(1)
#endif

#else // _MSC_VER

extern "C" {
__declspec(dllimport) void __stdcall DebugBreak(void);
}
#define Crash() DebugBreak()

#endif // _MSC_VER ndef

// For asserts we'd like to keep all the junk executed when an assert happens away from the
// important code in the function. One way of doing this is to put all the relevant code inside a
// lambda and force the compiler to not inline it. Unfortunately, MSVC seems to have no syntax to
// specify __declspec on lambda functions, so what we do instead is define a noinline wrapper
// template that calls the lambda. This seems to generate an extra instruction at the call-site
// compared to the ideal implementation (which wouldn't support ASSERT_MSG parameters), but is good
// enough for our purposes.
template <typename Fn>
#if defined(_MSC_VER)
[[msvc::noinline, noreturn]]
#elif defined(__GNUC__)
[[gnu::cold, gnu::noinline, noreturn]]
#endif
static void
assert_noinline_call(const Fn& fn) {
    fn();
    Crash();
    exit(1); // Keeps GCC's mouth shut about this actually returning
}

#define ASSERT(_a_)                                                                                \
    do                                                                                             \
        if (!(_a_)) {                                                                              \
            assert_noinline_call([] { SPDLOG_CRITICAL("Assertion Failed!"); });                    \
        }                                                                                          \
    while (0)

#define ASSERT_MSG(_a_, ...)                                                                       \
    do                                                                                             \
        if (!(_a_)) {                                                                              \
            assert_noinline_call([&] { SPDLOG_CRITICAL("Assertion Failed!\n" __VA_ARGS__); });     \
        }                                                                                          \
    while (0)

#define UNREACHABLE() assert_noinline_call([] { SPDLOG_CRITICAL("Unreachable code!"); })
#define UNREACHABLE_MSG(...)                                                                       \
    assert_noinline_call([&] { SPDLOG_CRITICAL("Unreachable code!\n" __VA_ARGS__); })

#ifdef _DEBUG
#define DEBUG_ASSERT(_a_) ASSERT(_a_)
#define DEBUG_ASSERT_MSG(_a_, ...) ASSERT_MSG(_a_, __VA_ARGS__)
#else // not debug
#define DEBUG_ASSERT(_a_)
#define DEBUG_ASSERT_MSG(_a_, _desc_, ...)
#endif

#define UNIMPLEMENTED() SPDLOG_CRITICAL("Unimplemented code!")
#define UNIMPLEMENTED_MSG(_a_, ...) SPDLOG_CRITICAL(_a_, __VA_ARGS__)
