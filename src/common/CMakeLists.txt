add_library(common STATIC
    alignment.h
    assert.h
    common_types.h
    file_util.cpp
    file_util.h
    log.cpp
    log.h
    pfr_helper.hpp
    ranges.h
    scope_exit.h
    swap.h
    temp_ptr.h
)

target_link_libraries(common PUBLIC boost)
target_link_libraries(common PRIVATE spdlog)
