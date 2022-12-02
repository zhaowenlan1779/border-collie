# CMake 3.12.4 required for 20 to be a valid value for CXX_STANDARD
cmake_minimum_required(VERSION 3.12.4)

if(${CMAKE_VERSION} VERSION_GREATER_EQUAL 3.15)
    # Add MSVC runtime library selection flags automatically:
    cmake_policy(SET CMP0091 OLD)

    # Don't override the warning flags in MSVC:
    cmake_policy(SET CMP0092 NEW)
endif()

if(POLICY CMP0076)
    cmake_policy(SET CMP0076 NEW)
endif()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/CMakeModules")

project(border_collie)
include(CMakeDependentOption)
include(ShaderCompile)

option(WARNINGS_AS_ERRORS "Treat warnings as errors" ON)
CMAKE_DEPENDENT_OPTION(COMPILE_WITH_DWARF "Add DWARF debugging information" ON "MINGW" OFF)

# Sanity check : Check that all submodules are present
# =======================================================================
function(check_submodules_present)
    file(READ "${PROJECT_SOURCE_DIR}/.gitmodules" gitmodules)
    string(REGEX MATCHALL "path *= *[^ \t\r\n]*" gitmodules ${gitmodules})

    foreach(module ${gitmodules})
        string(REGEX REPLACE "path *= *" "" module ${module})

        if(NOT EXISTS "${PROJECT_SOURCE_DIR}/${module}/.git")
            message(SEND_ERROR "Git submodule ${module} not found."
                "Please run: git submodule update --init --recursive")
        endif()
    endforeach()
endfunction()

check_submodules_present()

# Configure C++ standard
# ===========================
if(MSVC)
    add_compile_options(/std:c++latest)
    add_definitions(-D_HAS_DEPRECATED_RESULT_OF)
else()
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
endif()

# set up output paths for executable binaries
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/bin)

# System imported libraries
# ======================

# TODO: Is this necessary?

# Prefer the -pthread flag on Linux.
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

# Vulkan
find_package(Vulkan REQUIRED)
add_definitions(-DVULKAN_HPP_NO_CONSTRUCTORS)

# Detect current compilation architecture and create standard definitions
# =======================================================================
include(CheckSymbolExists)

function(detect_architecture symbol arch)
    if(NOT DEFINED ARCHITECTURE)
        set(CMAKE_REQUIRED_QUIET 1)
        check_symbol_exists("${symbol}" "" ARCHITECTURE_${arch})
        unset(CMAKE_REQUIRED_QUIET)

        # The output variable needs to be unique across invocations otherwise
        # CMake's crazy scope rules will keep it defined
        if(ARCHITECTURE_${arch})
            set(ARCHITECTURE "${arch}" PARENT_SCOPE)
            set(ARCHITECTURE_${arch} 1 PARENT_SCOPE)
            add_definitions(-DARCHITECTURE_${arch}=1)
        endif()
    endif()
endfunction()

if(NOT ENABLE_GENERIC)
    if(MSVC)
        detect_architecture("_M_AMD64" x86_64)
        detect_architecture("_M_IX86" x86)
        detect_architecture("_M_ARM" ARM)
        detect_architecture("_M_ARM64" ARM64)
    else()
        detect_architecture("__x86_64__" x86_64)
        detect_architecture("__i386__" x86)
        detect_architecture("__arm__" ARM)
        detect_architecture("__aarch64__" ARM64)
    endif()
endif()

if(NOT DEFINED ARCHITECTURE)
    set(ARCHITECTURE "GENERIC")
    set(ARCHITECTURE_GENERIC 1)
    add_definitions(-DARCHITECTURE_GENERIC=1)
endif()

message(STATUS "Target architecture: ${ARCHITECTURE}")

# Include source code
# ===================
add_subdirectory(externals)
add_subdirectory(src)
