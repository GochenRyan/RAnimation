project(SkeletalAnimCollision)

file(GLOB_RECURSE src CONFIGURE_DEPENDS "*.cpp" "*.h")

add_executable(SkeletalAnimCollision ${src})
setup_output_directories(SkeletalAnimCollision)

add_shader_make_target(
    SkeletalAnimCollisionShaders
    "${CMAKE_SOURCE_DIR}/Assets/SkeletalAnimCollision/shader"
    "${CMAKE_SOURCE_DIR}/Assets/SkeletalAnimCollision/shader/Shaders.cfg"
    "SkeletalAnimCollision"
)
add_dependencies(SkeletalAnimCollision SkeletalAnimCollisionShaders)

target_include_directories(SkeletalAnimCollision PRIVATE ${CMAKE_SOURCE_DIR}/Packages/Mathlib)
target_include_directories(SkeletalAnimCollision PRIVATE ${CMAKE_SOURCE_DIR}/Packages/glm)
target_include_directories(SkeletalAnimCollision PUBLIC include)

target_link_libraries(SkeletalAnimCollision PRIVATE detex::detex)
target_link_libraries(SkeletalAnimCollision PRIVATE fmt::fmt)
target_link_libraries(SkeletalAnimCollision PRIVATE NRI)
target_link_libraries(SkeletalAnimCollision PRIVATE SDL3::SDL3)
target_link_libraries(SkeletalAnimCollision PRIVATE imgui)
target_link_libraries(SkeletalAnimCollision PRIVATE ImGuiFileDialog)
target_link_libraries(SkeletalAnimCollision PRIVATE imgui_node_editor)
# Jolt Physics: per-bone collision bodies + queries (CMake/jolt_options.cmake adds the submodule).
target_link_libraries(SkeletalAnimCollision PRIVATE Jolt)
# TRS texture format + loader (Tools/TrsTexture). Models, animgraphs, state machine and level files are
# shared with SkeletalAnimControls (Assets/SkeletalAnimControls); only shaders and collision data live under
# Assets/SkeletalAnimCollision. The shared bake step runs before this sample builds.
target_link_libraries(SkeletalAnimCollision PRIVATE TrsTexture)
add_dependencies(SkeletalAnimCollision BakeTrsTextures)

# OpenUSD replaces assimp as this sample's asset loader (UsdSkel) and backs Import/Export Scene to USD.
# The reusable helpers (CMake/usd_options.cmake, CMake/vulkan_validation_options.cmake) link USD, deploy
# its plugin tree next to the exe, and point Debug's VK_LAYER_PATH at the vcpkg-pinned validation layer.
# Any future sample that needs USD / Vulkan-validation reuses the same two calls.
ranim_target_use_usd(SkeletalAnimCollision)
ranim_target_use_vulkan_validation_layer(SkeletalAnimCollision)

set_property(TARGET SkeletalAnimCollision PROPERTY COMPILE_WARNING_AS_ERROR OFF)

if(MSVC)
  set_property(TARGET SkeletalAnimCollision APPEND_STRING PROPERTY COMPILE_OPTIONS " /WX-")
  target_compile_options(SkeletalAnimCollision PRIVATE /wd4459)
endif()
