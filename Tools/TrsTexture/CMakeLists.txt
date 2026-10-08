project(TrsTexture)

# ---------------------------------------------------------------------------------------------------
# TrsTexture: the .trstex format, fingerprint and bake/sample code. glm + fmt only (no USD, no NRI), so
# both the baker and the runtime sample link it.
# ---------------------------------------------------------------------------------------------------
add_library(TrsTexture STATIC
    include/TrsTexture/TrsTexture.h
    include/TrsTexture/TrsTextureFile.h
    src/TrsTexture.cpp
    src/TrsTextureFile.cpp
)
target_include_directories(TrsTexture PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include ${CMAKE_SOURCE_DIR}/Packages/glm)
target_link_libraries(TrsTexture PUBLIC fmt::fmt)
set_property(TARGET TrsTexture PROPERTY COMPILE_WARNING_AS_ERROR OFF)
if(MSVC)
    target_compile_options(TrsTexture PRIVATE /WX- /wd4459)
endif()

# ---------------------------------------------------------------------------------------------------
# TrsTextureBaker: <Asset.asset.usda> -> <asset dir>/baked/<Stem>.trstex. Compiles the sample's USD loader
# and clip-sampling sources directly so node order, bind-pose decomposition and keyframe sampling are
# bit-identical with the runtime (header-only use of the sample's include tree; NRI is never linked).
# ---------------------------------------------------------------------------------------------------
set(RANIM_CONTROLS_DIR ${CMAKE_SOURCE_DIR}/Samples/SkeletalAnimControls)

add_executable(TrsTextureBaker
    src/ToolMain.cpp
    ${RANIM_CONTROLS_DIR}/src/Model/UsdModelLoader.cpp
    ${RANIM_CONTROLS_DIR}/src/Model/AnimChannel.cpp
    ${RANIM_CONTROLS_DIR}/src/Model/AnimClip.cpp
    ${RANIM_CONTROLS_DIR}/src/Model/ClipSampling.cpp
    ${RANIM_CONTROLS_DIR}/src/Model/TransformDecompose.cpp
    ${RANIM_CONTROLS_DIR}/src/Model/TrsSource.cpp
)
setup_output_directories(TrsTextureBaker)
target_include_directories(TrsTextureBaker PRIVATE
    ${RANIM_CONTROLS_DIR}/include
    ${RANIM_CONTROLS_DIR}/src/Model
    ${CMAKE_SOURCE_DIR}/Packages/glm
)
target_link_libraries(TrsTextureBaker PRIVATE TrsTexture fmt::fmt)
ranim_target_use_usd(TrsTextureBaker)
set_property(TARGET TrsTextureBaker PROPERTY COMPILE_WARNING_AS_ERROR OFF)
if(MSVC)
    target_compile_options(TrsTextureBaker PRIVATE /WX- /wd4459)
endif()

# ---------------------------------------------------------------------------------------------------
# Build-time bake of the sample asset ("compile-time / editing" bake on the whiteboard). The stamp file is
# the OUTPUT so an unchanged asset never re-runs the step; when any .usda/.json under the asset changes the
# baker runs, and its own fingerprint check still skips the write if nothing relevant changed.
# ---------------------------------------------------------------------------------------------------
set(RANIM_ANIMW_DIR ${CMAKE_SOURCE_DIR}/Assets/SkeletalAnimControls/animated_woman)
file(GLOB_RECURSE RANIM_ANIMW_INPUTS CONFIGURE_DEPENDS "${RANIM_ANIMW_DIR}/*.usda" "${RANIM_ANIMW_DIR}/*.json")
set(RANIM_ANIMW_STAMP ${CMAKE_CURRENT_BINARY_DIR}/Animated_Woman.trstex.stamp)

add_custom_command(
    OUTPUT ${RANIM_ANIMW_STAMP}
    COMMAND $<TARGET_FILE:TrsTextureBaker> "${RANIM_ANIMW_DIR}/Animated_Woman.asset.usda"
            -o "${RANIM_ANIMW_DIR}/baked/Animated_Woman.trstex" --rate 30
    COMMAND ${CMAKE_COMMAND} -E touch "${RANIM_ANIMW_STAMP}"
    DEPENDS TrsTextureBaker ${RANIM_ANIMW_INPUTS}
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Baking TRS texture: animated_woman -> baked/Animated_Woman.trstex"
    VERBATIM)
add_custom_target(BakeTrsTextures DEPENDS ${RANIM_ANIMW_STAMP})
