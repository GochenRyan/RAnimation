project(SkeletalAniamation)

file(GLOB_RECURSE src CONFIGURE_DEPENDS "*.cpp" "*.h")

add_executable(SkeletalAniamation ${src})
setup_output_directories(SkeletalAniamation)

target_include_directories(SkeletalAniamation PRIVATE ${CMAKE_SOURCE_DIR}/Packages/Mathlib)
target_include_directories(SkeletalAniamation PRIVATE ${CMAKE_SOURCE_DIR}/Packages/glm)
target_include_directories(SkeletalAniamation PUBLIC include)

target_link_libraries(SkeletalAniamation PRIVATE assimp)
target_link_libraries(SkeletalAniamation PRIVATE detex::detex)
target_link_libraries(SkeletalAniamation PRIVATE fmt::fmt)
target_link_libraries(SkeletalAniamation PRIVATE NRI_Shared)
target_link_libraries(SkeletalAniamation PRIVATE SDL3::SDL3)

set_property(TARGET SkeletalAniamation PROPERTY COMPILE_WARNING_AS_ERROR OFF)

if(MSVC)
  set_property(TARGET SkeletalAniamation APPEND_STRING PROPERTY COMPILE_OPTIONS " /WX-")
  target_compile_options(SkeletalAniamation PRIVATE /wd4459)
endif()