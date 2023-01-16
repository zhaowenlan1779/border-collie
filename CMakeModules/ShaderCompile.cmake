set(spv_version vulkan1.3)

function(target_shaders target)
    set(shader_files ${ARGN})

    foreach(shader ${shader_files})
        get_filename_component(file_name ${shader} NAME)
        get_filename_component(full_path ${shader} ABSOLUTE)

        set(output_dir ${CMAKE_BINARY_DIR}/shaders)
        set(output_file ${output_dir}/${target}/${shader}.spv)

        set(compiled_shaders ${compiled_shaders} ${output_file})
        set(compiled_shaders ${compiled_shaders} PARENT_SCOPE)
        message(STATUS "Output spv shader: ${output_file}")
        set_source_files_properties(${shader} PROPERTIES HEADER_FILE_ONLY TRUE)

        add_custom_command(
            OUTPUT ${output_file}
            COMMAND ${CMAKE_COMMAND} -E make_directory ${output_dir}
            COMMAND ${Vulkan_GLSLANG_VALIDATOR_EXECUTABLE} $<$<CONFIG:Debug>:-g> -I${PROJECT_SOURCE_DIR}/src --target-env ${spv_version} -V ${full_path} -o ${output_file}
            DEPENDS ${full_path}
        )
    endforeach()

    add_custom_target(
        ${target}_shaders
        DEPENDS ${compiled_shaders}
        SOURCES ${shader_files} ${shader_extra_files})

    # Before we can correctly detect shader file dependency, let's simply recompile all shader files everytime we build.
    add_custom_command(TARGET ${target}_shaders
        PRE_BUILD
        COMMAND ${CMAKE_COMMAND} -E touch_nocreate ${shader_files})

    add_dependencies(${target} ${target}_shaders)
endfunction()
