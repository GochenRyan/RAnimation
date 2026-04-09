# SA-B01-Q01-startup-to-first-frame

## 目标

解释程序如何从 `main` 走到可见首帧，并指出首帧前最关键的初始化对象。

## 要求

- 写出调用链：`main -> Application::init -> Renderer::Init`。
- 在 `Renderer::Init` 内列出初始化阶段（设备、队列、swapchain、descriptor、pipeline、UI）。
- 说明这些阶段的依赖关系（为什么不能随意调换）。

## 最小证据

至少引用以下文件中的实际函数：

- `src/Program.cpp`
- `src/Application/Application.cpp`
- `src/Renderer/Renderer.cpp`

## 交付

- 回答：`answers/SA-B01-Q01-startup-to-first-frame.md`
- 可选材料：`artifacts/SA-B01-Q01-startup-to-first-frame/`
