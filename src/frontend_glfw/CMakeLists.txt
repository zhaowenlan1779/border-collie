add_executable(frontend_glfw
    main.cpp
)

target_link_libraries(frontend_glfw PRIVATE common core glfw spdlog)
