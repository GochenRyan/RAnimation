# ============================================================
# Assimp - Release configuration
# ============================================================

# ---- location ----
set(OYI_ASSIMP_DIR ${CMAKE_SOURCE_DIR}/Engine/Packages/assimp)

# ---- build form ----
set(BUILD_SHARED_LIBS ON CACHE BOOL "" FORCE)

# ---- disable tests / tools ----
set(ASSIMP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_ASSIMP_TOOLS OFF CACHE BOOL "" FORCE)
set(ASSIMP_INSTALL OFF CACHE BOOL "" FORCE)

# ---- bring Assimp targets ----
add_subdirectory(${OYI_ASSIMP_DIR})

# ---- sanity check ----
if (NOT TARGET assimp AND NOT TARGET assimp::assimp)
    message(FATAL_ERROR
        "Assimp target not found after adding assimp subdirectory"
    )
endif()