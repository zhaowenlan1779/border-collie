add_executable(frontend_glfw
    border_collie.rc
    main.cpp
)

target_link_libraries(frontend_glfw PRIVATE common core glfw spdlog)

if(MSVC)
    target_link_libraries(frontend_glfw PRIVATE getopt)
endif()
