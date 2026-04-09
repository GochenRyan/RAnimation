# SkeletalAnimation Topics

## 核心主题

1. 启动与初始化路径
2. 首帧显示路径
3. `Renderer::Draw()` 主循环
4. `RRenderData` 数据流
5. Swapchain / queued frame / fence / semaphore
6. NRI ImGui 与 UI 渲染链路
7. Model / ModelInstance / InstanceSettings
8. 静态模型与骨骼动画渲染差异
9. Descriptor / Pipeline / Buffer 组织方式
10. Streamer 的使用与生命周期
11. Vulkan / NRI 同步问题定位
12. Device lost 案例复盘

## 建议学习顺序

### Phase 1

- 启动与初始化路径
- 首帧显示路径
- `Renderer::Draw()` 主循环

### Phase 2

- `RRenderData` 数据流
- Swapchain / queued frame / fence / semaphore
- NRI ImGui 与 UI 渲染链路

### Phase 3

- Model / ModelInstance / InstanceSettings
- 静态模型与骨骼动画渲染差异
- Descriptor / Pipeline / Buffer 组织方式

### Phase 4

- Streamer 的使用与生命周期
- Vulkan / NRI 同步问题定位
- Device lost 案例复盘
