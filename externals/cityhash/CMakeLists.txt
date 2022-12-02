add_library(cityhash
    city.cc
)
target_include_directories(cityhash PUBLIC .)
target_include_directories(cityhash PRIVATE ${CMAKE_CURRENT_BINARY_DIR})

include(TestBigEndian)
test_big_endian(CITYHASH_BIG_ENDIAN)

if(CITYHASH_BIG_ENDIAN)
    target_compile_definitions(cityhash PRIVATE -DWORDS_BIGENDIAN)
endif()

include(CheckCXXCompilerFlag)

function(CheckCompileLinkOption opt var prog)
    if(MSVC)
        # TODO: improve this...
        CHECK_CXX_COMPILER_FLAG(${opt} ${var})

    elseif(APPLE)
        message(STATUS "Performing Test ${var}")
        try_compile(COMMAND_SUCCESS ${CMAKE_BINARY_DIR} ${prog} COMPILE_DEFINITIONS ${opt})

        if(COMMAND_SUCCESS)
            set(${var} 1 PARENT_SCOPE)
            message(STATUS "Performing Test ${var} - Success")
        else()
            set(${var} 0 PARENT_SCOPE)
            message(STATUS "Performing Test ${var} - Failed")
        endif()

    else()
        message(STATUS "Performing Test ${var}")
        try_compile(COMMAND_SUCCESS ${CMAKE_BINARY_DIR} ${prog} COMPILE_DEFINITIONS ${opt})

        if(COMMAND_SUCCESS)
            set(${var} 1 PARENT_SCOPE)
            message(STATUS "Performing Test ${var} - Success")
        else()
            set(${var} 0 PARENT_SCOPE)
            message(STATUS "Performing Test ${var} - Failed")
        endif()
    endif()
endfunction(CheckCompileLinkOption)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang" OR CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID STREQUAL "Intel" OR CMAKE_CXX_COMPILER MATCHES "xlC")
    if(ARCHITECTURE_x86_64 OR ARCHITECTURE_x86)
        CheckCompileLinkOption("-msse4.2" CITYHASH_HAS_SSE42 "${CMAKE_CURRENT_SOURCE_DIR}/test_x86_sse42.cxx")

        if(NOT CITYHASH_HAS_SSE42)
            message(FATAL_ERROR "SSE4.2 is required for cityhash CRC to work")
        endif()

        if(CITYHASH_HAS_SSE42)
            set_source_files_properties("${CMAKE_CURRENT_SOURCE_DIR}/city.cc" PROPERTIES COMPILE_FLAGS "-msse4.2")
        endif()
    endif()
endif()
