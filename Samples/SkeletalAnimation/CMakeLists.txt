project(SkeletalAniamation)

file(GLOB_RECURSE src CONFIGURE_DEPENDS "*.cpp" "*.h")

add_executable(SkeletalAniamation ${src})
setup_output_directories(SkeletalAniamation)

target_link_libraries(SkeletalAniamation PRIVATE assimp)
target_link_libraries(SkeletalAniamation PRIVATE NRI_Shared)