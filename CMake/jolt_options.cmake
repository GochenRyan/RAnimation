# Jolt Physics (Packages/JoltPhysics submodule, pinned to v5.5.0) for the collision sample. Only
# SkeletalAnimCollision links it; nothing is linked globally. Jolt's Build/CMakeLists.txt only adds the
# library when it is not the top-level project, so its samples / unit tests / viewer are never built.
#
# Jolt propagates every JPH_* feature macro PUBLIC from the `Jolt` target, so a consumer automatically
# compiles its headers with the same configuration the library was built with. Keep that in mind before
# toggling anything below: the setting must be acceptable for the sample's own translation units too.

# The project forces MultiThreaded[Debug]DLL (root CMakeLists); Jolt defaults to the static runtime.
set(USE_STATIC_MSVC_RUNTIME_LIBRARY OFF CACHE BOOL "" FORCE)
# Keep CMake's per-config defaults rather than Jolt's own /Od /O2 sets (directory-scoped, but no reason
# to diverge from the rest of the tree).
set(OVERRIDE_CXX_FLAGS OFF CACHE BOOL "" FORCE)
# /Wall /WX inside Jolt breaks on compiler updates; the sample itself builds with /WX- as well.
set(ENABLE_ALL_WARNINGS OFF CACHE BOOL "" FORCE)
# No /GL in the static library, so the sample's link is not forced into LTCG (faster incremental links).
set(INTERPROCEDURAL_OPTIMIZATION OFF CACHE BOOL "" FORCE)
# Jolt would arm FP exceptions around PhysicsSystem::Update in Debug/Release. Animation data that
# reaches the physics world can legitimately carry inf/NaN while a feature is half-wired; trap in our
# own asserts instead of inside Jolt.
set(FLOATING_POINT_EXCEPTIONS_ENABLED OFF CACHE BOOL "" FORCE)
# We consume the in-tree target directly; no install/export rules needed.
set(ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
# Left at Jolt's defaults on purpose: DOUBLE_PRECISION OFF, CROSS_PLATFORM_DETERMINISTIC OFF,
# DEBUG_RENDERER_IN_DEBUG_AND_RELEASE ON (the sample draws Jolt bodies through DebugRenderer),
# PROFILER_IN_DEBUG_AND_RELEASE ON, CPP_EXCEPTIONS_ENABLED / CPP_RTTI_ENABLED OFF (Jolt headers do not
# need either; mixing with the sample's /EHsc /GR is supported by Jolt).

add_subdirectory(${CMAKE_SOURCE_DIR}/Packages/JoltPhysics/Build)
