set(_ra_had_build_shared_libs FALSE)
if(DEFINED BUILD_SHARED_LIBS)
    set(_ra_had_build_shared_libs TRUE)
    set(_ra_prev_build_shared_libs "${BUILD_SHARED_LIBS}")
endif()

# ImGuiFileDialog is used as an in-process UI helper by samples. Keep it
# static and keep the debug artifact name stable to avoid extra DLL deployment
# and config-specific filename handling in sample workspaces.
set(BUILD_SHARED_LIBS OFF)
add_subdirectory(${CMAKE_SOURCE_DIR}/Packages/ImGuiFileDialog)
if(_ra_had_build_shared_libs)
    set(BUILD_SHARED_LIBS "${_ra_prev_build_shared_libs}")
else()
    unset(BUILD_SHARED_LIBS)
endif()

target_link_libraries(ImGuiFileDialog PUBLIC imgui)

set_target_properties(ImGuiFileDialog PROPERTIES DEBUG_POSTFIX "")
