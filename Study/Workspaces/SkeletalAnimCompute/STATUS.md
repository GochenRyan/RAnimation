# SkeletalAnimCompute Status

## 当前状态

- Workspace 已初始化。
- 首篇核心 Wiki 已生成：`wiki/compute-pipeline-migration.md`。

## 当前关注点

- CPU 端仍负责动画采样和数据打包。
- GPU compute 负责局部 TRS 矩阵生成、父链累乘和最终 skinning matrix 输出。
- graphics skinning pass 读取 compute 写入的 bone matrix buffer。

## 后续可补专题

- GPU/CPU skinning matrix 对拍验证。
- Vulkan validation layer 与 NRI feature chain 版本问题。
- quaternion、GLM、HLSL 矩阵约定专项。
