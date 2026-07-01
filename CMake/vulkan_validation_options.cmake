# Reproducible Vulkan validation layer, delivered through the vcpkg manifest (vcpkg.json ->
# vulkan-validationlayers). NRI is built against newer Vulkan-Headers than a typical system SDK, so its
# Debug device creation chains feature structs the system validation layer may not know; pointing
# VK_LAYER_PATH at the vcpkg-pinned layer avoids that. Debug-only; Release runs without validation.
#
# Exposes ranim_target_use_vulkan_validation_layer(<target>) - reusable by any sample that creates an
# NRI/Vulkan device in Debug (today SkeletalAnimHelper; future samples can opt in the same way).
#
# This injects a per-config compile definition RANIM_VK_LAYER_PATH pointing at the vcpkg-installed layer
# dir; the sample's device-creation code sets VK_LAYER_PATH from it under _DEBUG before the VkInstance is
# created. No-op if the vcpkg install layout is unavailable.

function(ranim_target_use_vulkan_validation_layer target)
    if(NOT (DEFINED VCPKG_INSTALLED_DIR AND DEFINED VCPKG_TARGET_TRIPLET))
        return()
    endif()

    target_compile_definitions(${target} PRIVATE
        RANIM_VK_LAYER_PATH="$<IF:$<CONFIG:Debug>,${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/debug/bin,${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin>")
endfunction()
