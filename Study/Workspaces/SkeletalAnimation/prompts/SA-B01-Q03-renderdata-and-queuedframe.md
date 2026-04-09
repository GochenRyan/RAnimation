# SA-B01-Q03-renderdata-and-queuedframe

## 目标

建立 `RRenderData` 与 `QueuedFrame` 的结构化认知，说明它们如何支撑多帧并行。

## 要求

- 列出 `RRenderData` 中与帧同步最关键的字段。
- 解释 `GetQueuedFrameNum()`、`queuedFrameIndex`、`GetCurrentQueueFrame()` 的关系。
- 解释 `latencySleep()` 中等待 fence 与重置 command allocator 的时序意义。

## 最小证据

- `include/Model/RenderData.h`
- `src/Renderer/Renderer.cpp`

## 交付

- 回答：`answers/SA-B01-Q03-renderdata-and-queuedframe.md`
- 必选手推：`artifacts/SA-B01-Q03-renderdata-and-queuedframe/hand-derivation.md`
  - 内容要求：给出至少一个“3 帧队列”时的帧序号与 fence 值推演。
