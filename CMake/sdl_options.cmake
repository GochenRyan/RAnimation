add_subdirectory(${CMAKE_SOURCE_DIR}/Packages/SDL)

# ------------------------------------------------------------
# Build type
# ------------------------------------------------------------

set(SDL_SHARED ON  CACHE BOOL "Build SDL shared library" FORCE)
set(SDL_STATIC OFF CACHE BOOL "Disable static SDL library" FORCE)

# ------------------------------------------------------------
# Core platform systems (required)
# ------------------------------------------------------------

set(SDL_VIDEO       ON CACHE BOOL "" FORCE)  # windowing
set(SDL_EVENTS      ON CACHE BOOL "" FORCE)  # event system
set(SDL_TIMER       ON CACHE BOOL "" FORCE)
set(SDL_FILESYSTEM  ON CACHE BOOL "" FORCE)

# optional but often useful
set(SDL_LOADSO ON CACHE BOOL "" FORCE)

# ------------------------------------------------------------
# Gamepad / joystick support
# ------------------------------------------------------------

set(SDL_JOYSTICK ON CACHE BOOL "" FORCE)
set(SDL_HIDAPI   ON CACHE BOOL "" FORCE)

# vibration
set(SDL_HAPTIC OFF CACHE BOOL "" FORCE)

# gyro / motion sensors
set(SDL_SENSOR OFF CACHE BOOL "" FORCE)

# ------------------------------------------------------------
# Disable SDL rendering stacks
# (engine provides its own renderer)
# ------------------------------------------------------------

set(SDL_RENDER OFF CACHE BOOL "" FORCE)
set(SDL_GPU    OFF CACHE BOOL "" FORCE)

# ------------------------------------------------------------
# Disable subsystems not needed for engine platform layer
# ------------------------------------------------------------

set(SDL_AUDIO  OFF CACHE BOOL "" FORCE)
set(SDL_CAMERA OFF CACHE BOOL "" FORCE)

# ------------------------------------------------------------
# Reduce build noise / extras
# ------------------------------------------------------------

set(SDL_TESTS    OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)

# ------------------------------------------------------------
# Linux desktop compatibility
# ------------------------------------------------------------

# support both window systems
set(SDL_X11     ON CACHE BOOL "" FORCE)
set(SDL_WAYLAND ON CACHE BOOL "" FORCE)

# allow runtime loading of optional dependencies
set(SDL_DEPS_SHARED ON CACHE BOOL "" FORCE)