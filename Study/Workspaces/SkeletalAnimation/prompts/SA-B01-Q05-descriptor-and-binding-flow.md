# SA-B01-Q05-descriptor-and-binding-flow

## 目标

解释 sample 中 descriptor set / descriptor pool / pipeline layout 的绑定流程。

## 要求

- 说明 `createDescriptorSetLayouts -> createDescriptorPool -> createDescriptorSets -> updateDescriptors` 的链路。
- 区分场景渲染和 ImGui 渲染在 descriptor 使用上的不同。
- 指出至少两个“容易写错但会导致运行时问题”的点。

## 最小证据

- `src/Renderer/Renderer.cpp`
- `include/Model/RenderData.h`
- `Packages/NRI/Source/Shared/ImguiInterface.hpp`

## 交付

- 回答：`answers/SA-B01-Q05-descriptor-and-binding-flow.md`
- 可选材料：`artifacts/SA-B01-Q05-descriptor-and-binding-flow/`
