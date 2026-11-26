project(SkeletalAnimIK)

file(GLOB_RECURSE src CONFIGURE_DEPENDS "*.cpp" "*.h")

add_executable(SkeletalAnimIK ${src})
setup_output_directories(SkeletalAnimIK)

add_shader_make_target(
    SkeletalAnimIKShaders
    "${CMAKE_SOURCE_DIR}/Assets/SkeletalAnimIK/shader"
    "${CMAKE_SOURCE_DIR}/Assets/SkeletalAnimIK/shader/Shaders.cfg"
    "SkeletalAnimIK"
)
add_dependencies(SkeletalAnimIK SkeletalAnimIKShaders)

target_include_directories(SkeletalAnimIK PRIVATE ${CMAKE_SOURCE_DIR}/Packages/Mathlib)
target_include_directories(SkeletalAnimIK PRIVATE ${CMAKE_SOURCE_DIR}/Packages/glm)
target_include_directories(SkeletalAnimIK PUBLIC include)

target_link_libraries(SkeletalAnimIK PRIVATE detex::detex)
target_link_libraries(SkeletalAnimIK PRIVATE fmt::fmt)
target_link_libraries(SkeletalAnimIK PRIVATE NRI)
target_link_libraries(SkeletalAnimIK PRIVATE SDL3::SDL3)
target_link_libraries(SkeletalAnimIK PRIVATE imgui)
target_link_libraries(SkeletalAnimIK PRIVATE ImGuiFileDialog)
target_link_libraries(SkeletalAnimIK PRIVATE imgui_node_editor)
# Jolt Physics: per-bone collision bodies + queries (CMake/jolt_options.cmake adds the submodule).
target_link_libraries(SkeletalAnimIK PRIVATE Jolt)
# TRS texture format + loader (Tools/TrsTexture). Models and animgraphs are shared with SkeletalAnimControls
# (Assets/SkeletalAnimControls); the collision JSON is shared with SkeletalAnimCollision
# (Assets/SkeletalAnimCollision/collision). This sample keeps its shaders, character profiles and state
# machine in Assets/SkeletalAnimIK. The shared bake step runs before this sample builds.
target_link_libraries(SkeletalAnimIK PRIVATE TrsTexture)
add_dependencies(SkeletalAnimIK BakeTrsTextures)

# OpenUSD replaces assimp as this sample's asset loader (UsdSkel) and backs Import/Export Scene to USD.
# The reusable helpers (CMake/usd_options.cmake, CMake/vulkan_validation_options.cmake) link USD, deploy
# its plugin tree next to the exe, and point Debug's VK_LAYER_PATH at the vcpkg-pinned validation layer.
# Any future sample that needs USD / Vulkan-validation reuses the same two calls.
ranim_target_use_usd(SkeletalAnimIK)
ranim_target_use_vulkan_validation_layer(SkeletalAnimIK)

set_property(TARGET SkeletalAnimIK PROPERTY COMPILE_WARNING_AS_ERROR OFF)

if(MSVC)
  set_property(TARGET SkeletalAnimIK APPEND_STRING PROPERTY COMPILE_OPTIONS " /WX-")
  target_compile_options(SkeletalAnimIK PRIVATE /wd4459)
endif()
