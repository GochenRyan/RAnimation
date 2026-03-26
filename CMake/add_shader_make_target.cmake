function(add_shader_make_target TARGET_NAME SHADER_SOURCE_DIR SHADER_CONFIG OUTPUT_SUBDIR)
    set(SHADER_OUTPUT_PATH "${CMAKE_SOURCE_DIR}/_Shaders/${OUTPUT_SUBDIR}")

    file(GLOB_RECURSE LOCAL_SHADERS
        "${SHADER_SOURCE_DIR}/*.hlsl"
        "${SHADER_SOURCE_DIR}/*.hlsli"
    )

    set_source_files_properties(${LOCAL_SHADERS} PROPERTIES VS_TOOL_OVERRIDE "None")

    set(SHADERMAKE_GENERAL_ARGS
        --project "RAnimation"
        --binary
        --flatten
        --stripReflection
        --WX
        --sourceDir "${SHADER_SOURCE_DIR}"
        --ignoreConfigDir
        -c "${SHADER_CONFIG}"
        -o "${SHADER_OUTPUT_PATH}"
        -I "${SHADER_SOURCE_DIR}"
        -I "${CMAKE_SOURCE_DIR}/Packages/NRI/Include"
    )

    add_custom_target(${TARGET_NAME}
        COMMAND ${SHADERMAKE_PATH} -p DXIL --compiler "${SHADERMAKE_DXC_PATH}" ${SHADERMAKE_GENERAL_ARGS}
        COMMAND ${SHADERMAKE_PATH} -p SPIRV --compiler "${SHADERMAKE_DXC_VK_PATH}" ${SHADERMAKE_GENERAL_ARGS}
        DEPENDS ShaderMake
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        VERBATIM
        SOURCES ${LOCAL_SHADERS}
    )
endfunction()