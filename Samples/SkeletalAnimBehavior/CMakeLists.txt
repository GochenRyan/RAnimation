project(SkeletalAnimBehavior)

file(GLOB_RECURSE src CONFIGURE_DEPENDS "*.cpp" "*.h")

add_executable(SkeletalAnimBehavior ${src})
setup_output_directories(SkeletalAnimBehavior)

add_shader_make_target(
    SkeletalAnimBehaviorShaders
    "${CMAKE_SOURCE_DIR}/Assets/SkeletalAnimBehavior/shader"
    "${CMAKE_SOURCE_DIR}/Assets/SkeletalAnimBehavior/shader/Shaders.cfg"
    "SkeletalAnimBehavior"
)
add_dependencies(SkeletalAnimBehavior SkeletalAnimBehaviorShaders)

target_include_directories(SkeletalAnimBehavior PRIVATE ${CMAKE_SOURCE_DIR}/Packages/Mathlib)
target_include_directories(SkeletalAnimBehavior PRIVATE ${CMAKE_SOURCE_DIR}/Packages/glm)
target_include_directories(SkeletalAnimBehavior PUBLIC include)

target_link_libraries(SkeletalAnimBehavior PRIVATE detex::detex)
target_link_libraries(SkeletalAnimBehavior PRIVATE fmt::fmt)
target_link_libraries(SkeletalAnimBehavior PRIVATE NRI)
target_link_libraries(SkeletalAnimBehavior PRIVATE SDL3::SDL3)
target_link_libraries(SkeletalAnimBehavior PRIVATE imgui)
target_link_libraries(SkeletalAnimBehavior PRIVATE ImGuiFileDialog)
target_link_libraries(SkeletalAnimBehavior PRIVATE imgui_node_editor)
# Jolt Physics: per-bone collision bodies + queries (CMake/jolt_options.cmake adds the submodule).
target_link_libraries(SkeletalAnimBehavior PRIVATE Jolt)
# TRS texture format + loader (Tools/TrsTexture). Models and animgraphs are shared with SkeletalAnimControls
# (Assets/SkeletalAnimControls); the collision JSON is shared with SkeletalAnimCollision
# (Assets/SkeletalAnimCollision/collision). Assets/SkeletalAnimBehavior holds shader/, level/ (engine-written
# *.level.usda + Maya-written *.static.usda, see Tools/Maya/BehaviorScene), behavior/ (*.behaviortree.usda,
# global.blackboard.usda), profile/ (character profiles with this sample's montages) and statemachine/ (the
# character definition with CancelAction). The shared bake step runs before this sample builds.
target_link_libraries(SkeletalAnimBehavior PRIVATE TrsTexture)
add_dependencies(SkeletalAnimBehavior BakeTrsTextures)

# OpenUSD replaces assimp as this sample's asset loader (UsdSkel) and backs Import/Export Scene to USD.
# The reusable helpers (CMake/usd_options.cmake, CMake/vulkan_validation_options.cmake) link USD, deploy
# its plugin tree next to the exe, and point Debug's VK_LAYER_PATH at the vcpkg-pinned validation layer.
# Any future sample that needs USD / Vulkan-validation reuses the same two calls.
ranim_target_use_usd(SkeletalAnimBehavior)
ranim_target_use_vulkan_validation_layer(SkeletalAnimBehavior)

set_property(TARGET SkeletalAnimBehavior PROPERTY COMPILE_WARNING_AS_ERROR OFF)

if(MSVC)
  set_property(TARGET SkeletalAnimBehavior APPEND_STRING PROPERTY COMPILE_OPTIONS " /WX-")
  target_compile_options(SkeletalAnimBehavior PRIVATE /wd4459)
endif()
