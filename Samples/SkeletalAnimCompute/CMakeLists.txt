project(SkeletalAnimCompute)

file(GLOB_RECURSE src CONFIGURE_DEPENDS "*.cpp" "*.h")

add_executable(SkeletalAnimCompute ${src})
setup_output_directories(SkeletalAnimCompute)

add_shader_make_target(
    SkeletalAnimComputeShaders
    "${CMAKE_SOURCE_DIR}/Assets/SkeletalAnimCompute/shader"
    "${CMAKE_SOURCE_DIR}/Assets/SkeletalAnimCompute/shader/Shaders.cfg"
    "SkeletalAnimCompute"
)
add_dependencies(SkeletalAnimCompute SkeletalAnimComputeShaders)

target_include_directories(SkeletalAnimCompute PRIVATE ${CMAKE_SOURCE_DIR}/Packages/Mathlib)
target_include_directories(SkeletalAnimCompute PRIVATE ${CMAKE_SOURCE_DIR}/Packages/glm)
target_include_directories(SkeletalAnimCompute PUBLIC include)

target_link_libraries(SkeletalAnimCompute PRIVATE assimp)
target_link_libraries(SkeletalAnimCompute PRIVATE detex::detex)
target_link_libraries(SkeletalAnimCompute PRIVATE fmt::fmt)
target_link_libraries(SkeletalAnimCompute PRIVATE NRI)
target_link_libraries(SkeletalAnimCompute PRIVATE SDL3::SDL3)
target_link_libraries(SkeletalAnimCompute PRIVATE imgui)
target_link_libraries(SkeletalAnimCompute PRIVATE ImGuiFileDialog)

set_property(TARGET SkeletalAnimCompute PROPERTY COMPILE_WARNING_AS_ERROR OFF)

if(MSVC)
  set_property(TARGET SkeletalAnimCompute APPEND_STRING PROPERTY COMPILE_OPTIONS " /WX-")
  target_compile_options(SkeletalAnimCompute PRIVATE /wd4459)
endif()
