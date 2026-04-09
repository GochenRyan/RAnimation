# SA-B01-Q06-device-lost-diagnosis

## 目标

基于本项目真实上下文，给出一份可执行的 `device lost` 诊断方案。

## 要求

- 至少给出 3 条可验证的根因假设，并说明验证顺序。
- 每条假设都要给出“如何加日志/看什么状态/预期观察到什么”。
- 给出你认为最可能的一条并解释依据。

## 最小证据

- `src/Renderer/Renderer.cpp`
- `src/Renderer/UserInterface.cpp`
- `Packages/NRI/Source/Shared/ImguiInterface.hpp`
- `Packages/NRI/Source/Shared/StreamerInterface.hpp`

## 交付

- 回答：`answers/SA-B01-Q06-device-lost-diagnosis.md`
- 必选材料：`artifacts/SA-B01-Q06-device-lost-diagnosis/log.txt`
  - 放置你的关键日志片段或伪日志设计。
