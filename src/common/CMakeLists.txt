add_library(common STATIC
    alignment.h
    common_types.h
    file_util.cpp
    file_util.h
    log.cpp
    log.h
    ranges.h
    scope_exit.h
    temp_ptr.h
)

target_link_libraries(common PRIVATE spdlog)
