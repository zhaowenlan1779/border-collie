add_library(mikktspace
    mikktspace/mikktspace.c
    mikktspace/mikktspace.h
)

target_include_directories(mikktspace INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
