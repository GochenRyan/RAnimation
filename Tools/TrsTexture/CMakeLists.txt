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
# ranim_add_trs_bake(<asset dir> <asset stem>): bakes <dir>/<stem>.asset.usda -> <dir>/baked/<stem>.trstex
# and appends the stamp to RANIM_TRS_BAKE_STAMPS. Any .usda / .json under the asset directory is an input.
function(ranim_add_trs_bake asset_dir asset_stem)
    file(GLOB_RECURSE inputs CONFIGURE_DEPENDS "${asset_dir}/*.usda" "${asset_dir}/*.json")
    set(stamp ${CMAKE_CURRENT_BINARY_DIR}/${asset_stem}.trstex.stamp)
    get_filename_component(asset_leaf "${asset_dir}" NAME)
    add_custom_command(
        OUTPUT ${stamp}
        COMMAND $<TARGET_FILE:TrsTextureBaker> "${asset_dir}/${asset_stem}.asset.usda"
                -o "${asset_dir}/baked/${asset_stem}.trstex" --rate 30
        COMMAND ${CMAKE_COMMAND} -E touch "${stamp}"
        DEPENDS TrsTextureBaker ${inputs}
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        COMMENT "Baking TRS texture: ${asset_leaf} -> baked/${asset_stem}.trstex"
        VERBATIM)
    set(RANIM_TRS_BAKE_STAMPS ${RANIM_TRS_BAKE_STAMPS} ${stamp} PARENT_SCOPE)
endfunction()

set(RANIM_TRS_BAKE_STAMPS "")
set(RANIM_CONTROLS_ASSETS ${CMAKE_SOURCE_DIR}/Assets/SkeletalAnimControls)
ranim_add_trs_bake(${RANIM_CONTROLS_ASSETS}/animated_woman Animated_Woman)
# ual_mannequin is published by Tools/Maya/FBX2USD (ual_mannequin.ini); skip the bake when it is absent.
if(EXISTS ${RANIM_CONTROLS_ASSETS}/ual_mannequin/UAL_Mannequin.asset.usda)
    ranim_add_trs_bake(${RANIM_CONTROLS_ASSETS}/ual_mannequin UAL_Mannequin)
endif()
add_custom_target(BakeTrsTextures DEPENDS ${RANIM_TRS_BAKE_STAMPS})
