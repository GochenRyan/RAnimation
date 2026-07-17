# RAnimation

English | [简体中文](README.zh-CN.md)

A repository exploring game engine animation from the ground up.

## Samples

- **SkeletalAnimation** — CPU-side skinning: model loading via assimp, per-frame bone matrix evaluation and vertex skinning on the CPU, animation clip playback.
- **SkeletalAnimCompute** — GPU skinning: bone matrices and vertex skinning moved into compute shaders, with per-instance transforms for rendering many animated instances.
- **SkeletalAnimHelper** — editor-style sample on OpenUSD: loads UsdSkel assets (skeleton, skinning, animation clips), scene editing with selection/gizmo/undo-redo, and scene import/export to USD. Assets are published from Maya via `Tools/Maya/FBX2USD`.

## Prerequisites

- CMake ≥ 3.19 and Visual Studio 2022 (MSVC x64)
- [Vulkan SDK](https://vulkan.lunarg.com/) (provides the DXC used for SPIR-V shader compilation)

## Build

```
git clone <this repo>
cd RAnimation
git submodule update --init --recursive
setup_vcpkg.bat
cmake --build build --config Release
```

`setup_vcpkg.bat` bootstraps vcpkg into `Packages/vcpkg` and configures `build/` with the vcpkg toolchain (OpenUSD comes from the `vcpkg.json` manifest). **The first run compiles OpenUSD and can take 30+ minutes — once**; results are cached in `.vcpkg-cache/` and reused afterwards.

Executables land in `_Bin/<Config>/`, compiled shaders in `_Shaders/<SampleName>/`. Run from `_Bin/Release/`.

## ⚠️ Do not bare-configure `build/`

`CMAKE_TOOLCHAIN_FILE` lives only in the CMake cache, and a toolchain is only honored on the **first** configure of a build tree. So a bare `cmake -S . -B build` silently drops OpenUSD, and re-running `setup_vcpkg.bat` on the poisoned cache does not fix it. Recovery:

```
rm build/CMakeCache.txt
rm -r build/CMakeFiles
setup_vcpkg.bat
```

Keep `build/vcpkg_installed/` — nothing rebuilds, the follow-up build is incremental.
