cmake_minimum_required(VERSION 3.19)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)

project(RAnimation LANGUAGES CXX)

add_definitions(-D_UNICODE)
add_definitions(-DUNICODE)

if(PROJECT_BINARY_DIR STREQUAL PROJECT_SOURCE_DIR)
    message(WARNING "The Binary directory of CMake cannot be the same as source directory!")
endif()

if(WIN32)
    add_definitions(-DNOMINMAX -D_USE_MATH_DEFINES)
endif()

if (MSVC OR CMAKE_CXX_SIMULATE_ID STREQUAL "MSVC")
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
    add_compile_options(/Zc:preprocessor)
endif()

include(CMake/OutputDirectories.cmake)

include(CMake/add_shader_make_target.cmake)

include(CMake/Release/fmt_release.cmake)

include(CMake/Release/detex_release.cmake)

# Assimp defines _USE_MATH_DEFINES internally in some translation units.
# Keep the global define for the rest of the project, but temporarily remove it
# while configuring Assimp to avoid MSVC C4005 macro redefinition promoted by /WX.
if(WIN32)
    remove_definitions(-D_USE_MATH_DEFINES)
endif()

if (CMAKE_BUILD_TYPE STREQUAL "Debug")
include(CMake/Debug/assimp_debug.cmake)
else()
include(CMake/Release/assimp_release.cmake)
endif()

if(WIN32)
    add_definitions(-D_USE_MATH_DEFINES)
endif()

if (CMAKE_BUILD_TYPE STREQUAL "Debug")
include(CMake/Debug/nri_debug.cmake)
else()
include(CMake/Release/nri_release.cmake)
endif()

include(CMake/sdl_options.cmake)

include(CMake/shadermake_options.cmake)

add_library(imgui STATIC
    Packages/imgui/imgui.cpp
    Packages/imgui/imgui_draw.cpp
    Packages/imgui/imgui_tables.cpp
    Packages/imgui/imgui_widgets.cpp
    Packages/imgui/backends/imgui_impl_sdl3.cpp
)
target_include_directories(imgui
    PUBLIC
        ${CMAKE_SOURCE_DIR}/Packages/imgui
        ${CMAKE_SOURCE_DIR}/Packages/imgui/backends
)
target_link_libraries(imgui PUBLIC SDL3::SDL3)
add_library(imgui::imgui ALIAS imgui)

add_subdirectory(Packages/ImGuiFileDialog)
target_link_libraries(ImGuiFileDialog PUBLIC imgui)

# Assets
set(ASSETS_SRC_DIR "${CMAKE_SOURCE_DIR}/Assets")
add_compile_definitions(ASSETS_SRC_DIR="${ASSETS_SRC_DIR}")
set(SHADER_SRC_DIR "${CMAKE_SOURCE_DIR}/_Shaders")
add_compile_definitions(SHADER_SRC_DIR="${SHADER_SRC_DIR}")

add_subdirectory(Samples/SkeletalAnimation)

function(get_all_targets result dir)
    get_property(subdirs DIRECTORY ${dir} PROPERTY SUBDIRECTORIES)

    foreach(subdir ${subdirs})
        get_all_targets(${result} ${subdir})
    endforeach()

    get_property(current_targets DIRECTORY ${dir} PROPERTY BUILDSYSTEM_TARGETS)

    set(${result} ${${result}} ${current_targets} PARENT_SCOPE)
endfunction()

get_all_targets(ALL_TARGETS ${CMAKE_SOURCE_DIR})

foreach(t ${ALL_TARGETS})
    setup_output_directories(${t})
endforeach()
