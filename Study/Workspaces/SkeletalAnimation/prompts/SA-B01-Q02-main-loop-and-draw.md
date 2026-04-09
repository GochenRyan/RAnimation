# SA-B01-Q02-main-loop-and-draw

## 目标

解释每一帧的执行顺序，以及 `deltaTime` 如何参与动画更新。

## 要求

- 解释 `Application::MainLoop()` 的每轮步骤。
- 解释 `Renderer::Draw(deltaTime)` 内 CPU 端主要工作（矩阵更新、UI 生成、命令录制、提交展示）。
- 说明 `deltaTime` 的计算方式与安全处理（例如最小值保护）对动画更新的影响。

## 最小证据

- `src/Application/Application.cpp`
- `src/Renderer/Renderer.cpp`
- `src/Model/ModelInstance.cpp`

## 交付

- 回答：`answers/SA-B01-Q02-main-loop-and-draw.md`
- 可选材料：`artifacts/SA-B01-Q02-main-loop-and-draw/`
