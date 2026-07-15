# RAnimation

[English](README.md) | 简体中文

从零开始探索游戏引擎动画的实验仓库。

## Samples

- **SkeletalAnimation** —— CPU 蒙皮（assimp 加载模型，每帧在 CPU 上计算骨骼矩阵并做顶点蒙皮，播放动画片段）
- **SkeletalAnimCompute** —— GPU 蒙皮（骨骼矩阵与顶点蒙皮移入 compute shader，配合逐实例变换渲染大量动画实例）
- **SkeletalAnimHelper** —— 基于 OpenUSD 的编辑器式示例（加载 UsdSkel 资产——骨架/蒙皮/动画片段，支持选中/gizmo/撤销重做的场景编辑，以及场景的 USD 导入导出）。资产由 Maya 经 `Tools/Maya/FBX2USD` 发布。

## 前置条件

- CMake ≥ 3.19 和 Visual Studio 2022（MSVC x64）
- [Vulkan SDK](https://vulkan.lunarg.com/)（提供编译 SPIR-V 着色器所需的 DXC）

## 构建

```
git clone <this repo>
cd RAnimation
git submodule update --init --recursive
setup_vcpkg.bat
cmake --build build --config Release
```

`setup_vcpkg.bat` 会把 vcpkg 引导到 `Packages/vcpkg`，并用 vcpkg 工具链配置 `build/`（OpenUSD 由 `vcpkg.json` manifest 提供）。**首次运行会编译 OpenUSD，可能耗时 30 分钟以上——仅此一次**；结果缓存在 `.vcpkg-cache/`，之后复用。

可执行文件输出到 `_Bin/<Config>/`，编译后的着色器在 `_Shaders/<SampleName>/`。从 `_Bin/Release/` 运行。

## ⚠️ 不要对 `build/` 裸配置

`CMAKE_TOOLCHAIN_FILE` 只存在于 CMake 缓存中，且工具链只在构建树**首次**配置时生效。因此裸 `cmake -S . -B build` 会悄悄丢掉 OpenUSD，而在被污染的缓存上重跑 `setup_vcpkg.bat` 也救不回来。恢复方法：

```
rm build/CMakeCache.txt
rm -r build/CMakeFiles
setup_vcpkg.bat
```

保留 `build/vcpkg_installed/`——不会重新编译任何包，后续构建是增量的。
