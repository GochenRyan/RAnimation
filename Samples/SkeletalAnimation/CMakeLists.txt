project(SkeletalAnimation)

file(GLOB_RECURSE src CONFIGURE_DEPENDS "*.cpp" "*.h")

add_executable(SkeletalAnimation ${src})
setup_output_directories(SkeletalAnimation)

add_shader_make_target(
    SkeletalAnimationShaders
    "${CMAKE_SOURCE_DIR}/Assets/SkeletalAnimation/shader"
    "${CMAKE_SOURCE_DIR}/Assets/SkeletalAnimation/shader/Shaders.cfg"
    "SkeletalAnimation"
)
add_dependencies(SkeletalAnimation SkeletalAnimationShaders)

target_include_directories(SkeletalAnimation PRIVATE ${CMAKE_SOURCE_DIR}/Packages/Mathlib)
target_include_directories(SkeletalAnimation PRIVATE ${CMAKE_SOURCE_DIR}/Packages/glm)
target_include_directories(SkeletalAnimation PUBLIC include)

target_link_libraries(SkeletalAnimation PRIVATE assimp)
target_link_libraries(SkeletalAnimation PRIVATE detex::detex)
target_link_libraries(SkeletalAnimation PRIVATE fmt::fmt)
target_link_libraries(SkeletalAnimation PRIVATE NRI)
target_link_libraries(SkeletalAnimation PRIVATE SDL3::SDL3)
target_link_libraries(SkeletalAnimation PRIVATE imgui)
target_link_libraries(SkeletalAnimation PRIVATE ImGuiFileDialog)

set_property(TARGET SkeletalAnimation PROPERTY COMPILE_WARNING_AS_ERROR OFF)

if(MSVC)
  set_property(TARGET SkeletalAnimation APPEND_STRING PROPERTY COMPILE_OPTIONS " /WX-")
  target_compile_options(SkeletalAnimation PRIVATE /wd4459)
endif()
