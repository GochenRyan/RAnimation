# OpenUSD (Pixar) is delivered through the vcpkg manifest (vcpkg.json). It is intentionally NOT linked
# globally: only samples that opt in consume it. Today that is SkeletalAnimHelper, but any future sample
# that needs USD can reuse this by calling ranim_target_use_usd(<target>) from its CMakeLists.
#
# This module probes for USD and exposes:
#   RANIM_HAS_USD          - ON when find_package(pxr) succeeded
#   RANIM_USD_LIBS         - the pxr targets to link against
#   RANIM_USD_PLUGIN_DIR   - the installed USD plugin resource tree (per-config generator expression)
#   ranim_target_use_usd(target) - links USD into <target>, injects the plugin-dir compile definition,
#                                  and deploys the plugin tree next to the exe (self-contained _Bin).
#
# When USD is unavailable (e.g. configuring without the vcpkg toolchain), the build degrades cleanly:
# the root CMakeLists simply does not add the USD-dependent samples, rather than hard-failing.

find_package(pxr CONFIG QUIET)

if(pxr_FOUND)
    set(RANIM_HAS_USD ON)

    # OpenUSD exports unnamespaced targets (usd, usdGeom, usdSkel, ...).
    # These cover stage I/O (sdf/usd), geometry (usdGeom), skinning (usdSkel),
    # materials (usdShade) and the value/plugin plumbing they need.
    set(RANIM_USD_LIBS usd usdGeom usdSkel usdShade sdf vt gf tf arch plug js
        CACHE INTERNAL "pxr targets used by RAnimation")

    # Per-config plugin resource directory inside the vcpkg install tree. Injected as a compile
    # definition; USD-consuming code registers the individual <plugin>/resources/plugInfo.json files
    # under it via PlugRegistry, so plugin discovery does not depend on cwd or DLL-adjacency.
    #
    # NOTE: vcpkg's USD layout puts the per-plugin resource dirs next to the DLLs in bin/usd (debug/bin/usd)
    # — NOT in lib/usd, where only an orphaned aggregator plugInfo.json lives. Point at bin/usd.
    if(DEFINED VCPKG_INSTALLED_DIR AND DEFINED VCPKG_TARGET_TRIPLET)
        set(RANIM_USD_PLUGIN_DIR
            "$<IF:$<CONFIG:Debug>,${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/debug/bin/usd,${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin/usd>"
            CACHE INTERNAL "USD plugin resource dir (generator expression)")
    else()
        set(RANIM_USD_PLUGIN_DIR "" CACHE INTERNAL "USD plugin resource dir")
    endif()

    message(STATUS "OpenUSD found via vcpkg (pxr) - USD-dependent samples will load USD natively")
else()
    set(RANIM_HAS_USD OFF)
    message(STATUS
        "OpenUSD (pxr) not found - USD-dependent samples will be skipped. "
        "Run setup_vcpkg.bat (or configure with the vcpkg toolchain) to enable them.")
endif()

# Wires OpenUSD into a target. Call from any sample that loads/writes USD; the vcpkg-delivered DLLs are
# copied next to the exe automatically (applocal), and this adds the plugin resource tree so _Bin/<Config>
# is self-contained. No-op with a warning if USD was not found.
function(ranim_target_use_usd target)
    if(NOT RANIM_HAS_USD)
        message(WARNING "ranim_target_use_usd(${target}) called but OpenUSD was not found; skipping.")
        return()
    endif()

    target_link_libraries(${target} PRIVATE ${RANIM_USD_LIBS})

    if(RANIM_USD_PLUGIN_DIR)
        target_compile_definitions(${target} PRIVATE
            RANIM_USD_PLUGIN_DIR="${RANIM_USD_PLUGIN_DIR}")

        # Deploy the USD plugin resource tree next to the exe so _Bin/<Config> is relocatable. The runtime
        # prefers <exeDir>/usd and only falls back to RANIM_USD_PLUGIN_DIR (the vcpkg tree) if absent.
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different
                    "${RANIM_USD_PLUGIN_DIR}" "$<TARGET_FILE_DIR:${target}>/usd"
            COMMENT "Deploying USD plugin tree to $<TARGET_FILE_DIR:${target}>/usd"
            VERBATIM)
    endif()
endfunction()
