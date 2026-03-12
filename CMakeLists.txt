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

include(CMake/Release/fmt_release.cmake)
setup_output_directories(fmt)

include(CMake/Release/detex_release.cmake)
setup_output_directories(detex)

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

if(TARGET assimp)
setup_output_directories(assimp)
endif()

if (CMAKE_BUILD_TYPE STREQUAL "Debug")
include(CMake/Debug/nri_debug.cmake)
setup_output_directories(NRI)
else()
include(CMake/Release/nri_release.cmake)
setup_output_directories(NRI)
endif()

include(CMake/sdl_options.cmake)
setup_output_directories(SDL3-shared)

# Assets
set(ASSETS_SRC_DIR "${CMAKE_SOURCE_DIR}/Assets")
add_compile_definitions(ASSETS_SRC_DIR="${ASSETS_SRC_DIR}")

add_subdirectory(Samples/SkeletalAnimation)