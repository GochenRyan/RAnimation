project(SkeletalAnimControls)

file(GLOB_RECURSE src CONFIGURE_DEPENDS "*.cpp" "*.h")

add_executable(SkeletalAnimControls ${src})
setup_output_directories(SkeletalAnimControls)

add_shader_make_target(
    SkeletalAnimControlsShaders
    "${CMAKE_SOURCE_DIR}/Assets/SkeletalAnimControls/shader"
    "${CMAKE_SOURCE_DIR}/Assets/SkeletalAnimControls/shader/Shaders.cfg"
    "SkeletalAnimControls"
)
add_dependencies(SkeletalAnimControls SkeletalAnimControlsShaders)

target_include_directories(SkeletalAnimControls PRIVATE ${CMAKE_SOURCE_DIR}/Packages/Mathlib)
target_include_directories(SkeletalAnimControls PRIVATE ${CMAKE_SOURCE_DIR}/Packages/glm)
target_include_directories(SkeletalAnimControls PUBLIC include)

target_link_libraries(SkeletalAnimControls PRIVATE detex::detex)
target_link_libraries(SkeletalAnimControls PRIVATE fmt::fmt)
target_link_libraries(SkeletalAnimControls PRIVATE NRI)
target_link_libraries(SkeletalAnimControls PRIVATE SDL3::SDL3)
target_link_libraries(SkeletalAnimControls PRIVATE imgui)
target_link_libraries(SkeletalAnimControls PRIVATE ImGuiFileDialog)
target_link_libraries(SkeletalAnimControls PRIVATE imgui_node_editor)
# TRS texture format + loader (Tools/TrsTexture). The baked asset is produced by the BakeTrsTextures step
# (fingerprint-gated, so a no-change build does nothing) before the sample builds.
target_link_libraries(SkeletalAnimControls PRIVATE TrsTexture)
add_dependencies(SkeletalAnimControls BakeTrsTextures)

# OpenUSD replaces assimp as this sample's asset loader (UsdSkel) and backs Import/Export Scene to USD.
# The reusable helpers (CMake/usd_options.cmake, CMake/vulkan_validation_options.cmake) link USD, deploy
# its plugin tree next to the exe, and point Debug's VK_LAYER_PATH at the vcpkg-pinned validation layer.
# Any future sample that needs USD / Vulkan-validation reuses the same two calls.
ranim_target_use_usd(SkeletalAnimControls)
ranim_target_use_vulkan_validation_layer(SkeletalAnimControls)

set_property(TARGET SkeletalAnimControls PROPERTY COMPILE_WARNING_AS_ERROR OFF)

if(MSVC)
  set_property(TARGET SkeletalAnimControls APPEND_STRING PROPERTY COMPILE_OPTIONS " /WX-")
  target_compile_options(SkeletalAnimControls PRIVATE /wd4459)
endif()
