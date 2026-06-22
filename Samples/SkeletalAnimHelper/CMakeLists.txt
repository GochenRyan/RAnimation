project(SkeletalAnimHelper)

file(GLOB_RECURSE src CONFIGURE_DEPENDS "*.cpp" "*.h")

add_executable(SkeletalAnimHelper ${src})
setup_output_directories(SkeletalAnimHelper)

add_shader_make_target(
    SkeletalAnimHelperShaders
    "${CMAKE_SOURCE_DIR}/Assets/SkeletalAnimHelper/shader"
    "${CMAKE_SOURCE_DIR}/Assets/SkeletalAnimHelper/shader/Shaders.cfg"
    "SkeletalAnimHelper"
)
add_dependencies(SkeletalAnimHelper SkeletalAnimHelperShaders)

target_include_directories(SkeletalAnimHelper PRIVATE ${CMAKE_SOURCE_DIR}/Packages/Mathlib)
target_include_directories(SkeletalAnimHelper PRIVATE ${CMAKE_SOURCE_DIR}/Packages/glm)
target_include_directories(SkeletalAnimHelper PUBLIC include)

target_link_libraries(SkeletalAnimHelper PRIVATE assimp)
target_link_libraries(SkeletalAnimHelper PRIVATE detex::detex)
target_link_libraries(SkeletalAnimHelper PRIVATE fmt::fmt)
target_link_libraries(SkeletalAnimHelper PRIVATE NRI)
target_link_libraries(SkeletalAnimHelper PRIVATE SDL3::SDL3)
target_link_libraries(SkeletalAnimHelper PRIVATE imgui)
target_link_libraries(SkeletalAnimHelper PRIVATE ImGuiFileDialog)

set_property(TARGET SkeletalAnimHelper PROPERTY COMPILE_WARNING_AS_ERROR OFF)

if(MSVC)
  set_property(TARGET SkeletalAnimHelper APPEND_STRING PROPERTY COMPILE_OPTIONS " /WX-")
  target_compile_options(SkeletalAnimHelper PRIVATE /wd4459)
endif()
