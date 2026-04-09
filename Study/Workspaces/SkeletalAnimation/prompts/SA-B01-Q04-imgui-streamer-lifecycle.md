# SA-B01-Q04-imgui-streamer-lifecycle

## 目标

解释 ImGui 上传与绘制链路，以及 `EndStreamerFrame()` 在资源生命周期中的作用。

## 要求

- 说明 `CmdCopyImguiData` 与 `CmdDrawImgui` 的职责分工。
- 说明 streamer 在每帧末尾为什么必须推进。
- 从长期运行角度解释“遗漏 `EndStreamerFrame`”会带来的风险。

## 最小证据

- `src/Renderer/Renderer.cpp`
- `src/Renderer/UserInterface.cpp`
- `Packages/NRI/Source/Shared/ImguiInterface.hpp`
- `Packages/NRI/Source/Shared/StreamerInterface.hpp`

## 交付

- 回答：`answers/SA-B01-Q04-imgui-streamer-lifecycle.md`
- 必选材料：`artifacts/SA-B01-Q04-imgui-streamer-lifecycle/notes.md`
  - 内容要求：简要列出你认为最关键的 3 个生命周期节点。
