# SkeletalAnimation Roadmap

## Phase 1: 启动到首帧

### 学习目标

- 能完整复述启动链路（`main -> Application -> Platform -> Renderer`）。
- 能解释 `Renderer::Init()` 中主要初始化步骤为什么按当前顺序执行。

### 必读文件

- `src/Program.cpp`
- `src/Application/Application.cpp`
- `src/Renderer/Renderer.cpp`（重点看 `Init`）

### 验证标准

- 你能写出首帧前必须准备好的核心对象（Device/Queue/Swapchain/Descriptors/UI）。

## Phase 2: 每帧渲染与同步

### 学习目标

- 读懂 `Renderer::Draw()` 的数据流与命令流。
- 说清楚 `queued frame`、`acquire/present semaphore`、`frame fence` 的作用关系。

### 必读文件

- `src/Renderer/Renderer.cpp`（重点看 `Draw`、`latencySleep`）
- `include/Model/RenderData.h`

### 验证标准

- 你能解释为什么每帧都需要 `Acquire -> Submit -> Present`，以及 CPU/GPU 如何错帧并行。

## Phase 3: UI、Streamer 与资源生命周期

### 学习目标

- 理解 ImGui 数据上传、绘制命令、descriptor 绑定的链路。
- 理解 `EndStreamerFrame()` 的必要性和缺失后的风险。

### 必读文件

- `src/Renderer/UserInterface.cpp`
- `src/Renderer/Renderer.cpp`（`CmdCopyImguiData`、`CmdDrawImgui` 附近）
- `Packages/NRI/Source/Shared/ImguiInterface.hpp`
- `Packages/NRI/Source/Shared/StreamerInterface.hpp`

### 验证标准

- 你能从源码解释“为什么不调用 `EndStreamerFrame` 会导致长期运行后崩溃风险上升”。

## Phase 4: 模型实例与动画路径

### 学习目标

- 读懂 `Model` / `ModelInstance` / `ModelAndInstanceData` 的关系。
- 读懂静态实例与骨骼动画实例在渲染路径上的分流。

### 必读文件

- `src/Model/Model.cpp`
- `src/Model/ModelInstance.cpp`
- `include/Model/ModelAndInstanceData.h`
- `src/Renderer/Renderer.cpp`（实例遍历和两条绘制路径）

### 验证标准

- 你能解释 `modelStride`、`worldPosOffset` 在两条路径里的意义差异。

## 建议执行节奏

1. 先完成 `SA-B01` 的 Q1-Q3（启动、主循环、数据结构）。
2. 再做 Q4-Q6（UI/streamer、descriptor、device lost 诊断）。
3. 批改后按 `STATUS.md` 的薄弱项补一个专题复盘。
