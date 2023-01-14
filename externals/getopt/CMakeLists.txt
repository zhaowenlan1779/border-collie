add_library(getopt
    getopt/getopt.c
    getopt/getopt.h
)

target_include_directories(getopt INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/getopt)
