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