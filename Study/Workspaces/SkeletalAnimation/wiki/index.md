# SkeletalAnimation Wiki Index

## 1. 学习目标

这个 workspace 的目标不是“知道有这些文件”，而是能做到：

- 说清楚从进程启动到首帧显示的完整调用链。
- 说清楚每一帧里 CPU 端生成了什么数据、提交了什么命令、如何同步。
- 能从日志和源码定位渲染崩溃（尤其是 `QueueSubmit` / `device lost`）。
- 能给出可执行的修复方案并解释为什么有效。

## 2. 入口与关键文件

### 2.1 启动入口

- `Samples/SkeletalAnimation/src/Program.cpp`
- `Samples/SkeletalAnimation/src/Application/Application.cpp`

### 2.2 平台与事件

- `Samples/SkeletalAnimation/src/Platform/SDL/SDLPlatform.cpp`
- `Samples/SkeletalAnimation/include/Platform/SDL/SDLPlatform.h`

### 2.3 渲染主链路

- `Samples/SkeletalAnimation/src/Renderer/Renderer.cpp`
- `Samples/SkeletalAnimation/include/Renderer/Renderer.h`
- `Samples/SkeletalAnimation/src/Renderer/UserInterface.cpp`

### 2.4 数据中枢

- `Samples/SkeletalAnimation/include/Model/RenderData.h`
- `Samples/SkeletalAnimation/include/Model/ModelAndInstanceData.h`

### 2.5 模型与动画

- `Samples/SkeletalAnimation/src/Model/Model.cpp`
- `Samples/SkeletalAnimation/src/Model/ModelInstance.cpp`

## 3. 最小调用链地图

```text
main()
 -> Application::init()
    -> SDLPlatform::Initialize()
    -> SDLPlatform::CreateWindow()
    -> Renderer::Init()
       -> initDevice/initNRI/createStreamer/createSwapchain/...
       -> UserInterface::Init()

Application::MainLoop()
 -> while (!window->ShouldClose())
    -> Renderer::SetSize()
    -> Renderer::Draw(deltaTime)
       -> latencySleep + AcquireNextTexture
       -> update matrices + UI frame + ImGui render
       -> BeginCommandBuffer + CmdCopyImguiData
       -> scene pass (optional) + imgui pass
       -> QueueSubmit + QueuePresent + EndStreamerFrame
    -> SDLPlatform::PumpEvents()
```

## 4. 推荐学习顺序

先看路线图：

- `wiki/roadmap.md`

然后做第一批验证题：

- `prompts/SA-B01-index.md`

## 5. 作答与材料放置规范

题目、答案、批改、手推材料统一使用同一个 ID。

例如 `SA-B01-Q03-renderdata-and-queuedframe`：

- 题目：`prompts/SA-B01-Q03-renderdata-and-queuedframe.md`
- 回答：`answers/SA-B01-Q03-renderdata-and-queuedframe.md`
- 材料目录：`artifacts/SA-B01-Q03-renderdata-and-queuedframe/`
- 批改：`reviews/SA-B01-Q03-renderdata-and-queuedframe.md`

## 6. 当前执行状态

- 协议层已建立。
- 核心学习包（本文件 + roadmap + SA-B01）已生成。
- 下一步：你开始按 `SA-B01` 作答，我按同名 ID 批改。
