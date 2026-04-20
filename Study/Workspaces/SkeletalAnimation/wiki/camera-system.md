# SkeletalAnimation 相机系统

## 1. 这篇文档回答什么问题

这套 sample 的相机并不复杂，但它和动画、导入模型后的自动对焦、屏幕上下翻转这几个问题直接相关。

如果只把它看成“几个 slider 控一个 view matrix”，很容易在排查时误判成：

- 模型导入坐标系错了
- 动画 clip 全部倒置
- light 太暗所以看不见

这篇文档的目标是把相机链路拆开，说明：

- CPU 端到底保存了哪些相机参数
- 每帧如何从参数生成 `forward / target / view / projection`
- 这些参数在 UI 里各代表什么
- 导入模型后为什么会自动看向模型
- 之前“模型整体上下颠倒”的根因为什么是投影矩阵，而不是模型或动画转换

## 2. 代码入口

相机的核心逻辑集中在两个位置。

### 2.1 运行时参数存储

- [RenderData.h](/S:/Github/RAnimation/Samples/SkeletalAnimation/include/Model/RenderData.h#L86)

这里保存了相机相关的状态：

- `rdFieldOfView`
- `rdViewAzimuth`
- `rdViewElevation`
- `rdCameraWorldPosition`

默认值是：

```cpp
int rdFieldOfView = 60;
float rdViewAzimuth = 330.0f;
float rdViewElevation = -20.0f;
glm::vec3 rdCameraWorldPosition = glm::vec3(2.0f, 5.0f, 7.0f);
```

### 2.2 每帧矩阵生成

- [Renderer.cpp](/S:/Github/RAnimation/Samples/SkeletalAnimation/src/Renderer/Renderer.cpp#L214)

每一帧 `Renderer::Draw()` 开头都会根据这些参数生成：

- 朝向向量 `forward`
- 观察目标点 `target`
- 视图矩阵 `viewMatrix`
- 投影矩阵 `projectionMatrix`

核心代码如下：

```cpp
glm::vec3 forward = glm::normalize(glm::vec3(std::cos(glm::radians(mRenderData.rdViewElevation)) *
                                                     std::cos(glm::radians(mRenderData.rdViewAzimuth)),
                                             std::sin(glm::radians(mRenderData.rdViewElevation)),
                                             std::cos(glm::radians(mRenderData.rdViewElevation)) *
                                                     std::sin(glm::radians(mRenderData.rdViewAzimuth))));
glm::vec3 target = mRenderData.rdCameraWorldPosition + forward;

matrices.viewMatrix = glm::lookAtRH(mRenderData.rdCameraWorldPosition, target, glm::vec3(0.0f, 1.0f, 0.0f));
matrices.projectionMatrix =
        glm::perspectiveRH_ZO(glm::radians(static_cast<float>(mRenderData.rdFieldOfView)),
                              static_cast<float>(mRenderData.rdOutputResolution.x) /
                                      static_cast<float>(mRenderData.rdOutputResolution.y),
                              0.1f,
                              500.0f);
```

## 3. 相机模型是什么

这不是自由飞行相机，也不是完整 orbit camera。

它本质上是一个“位置 + 两个欧拉角”的朝向相机：

- 位置由 `rdCameraWorldPosition` 给出
- 朝向由 `azimuth + elevation` 给出
- 没有显式保存 `target`
- `target` 是每帧用 `position + forward` 临时算出来的

所以这套系统更准确的描述是：

- 一个右手系 look-at 相机
- 用球坐标风格的两个角度生成前向向量
- 再由 `glm::lookAtRH` 转成 view matrix

## 4. 朝向算法

### 4.1 参数定义

- `Azimuth`：水平方向角
- `Elevation`：俯仰角

代码中把角度转弧度后，按下面的公式生成前向向量：

```text
forward.x = cos(elevation) * cos(azimuth)
forward.y = sin(elevation)
forward.z = cos(elevation) * sin(azimuth)
```

然后再做一次归一化。

这意味着：

- `Elevation = 0` 时，相机只在水平面内转
- `Elevation > 0` 时，视线向上抬
- `Elevation < 0` 时，视线向下压
- `Azimuth` 控制水平面内绕 Y 轴旋转

### 4.2 为什么 UI 把 elevation 限制在 `[-89, 89]`

UI 代码见 [UserInterface.cpp](/S:/Github/RAnimation/Samples/SkeletalAnimation/src/Renderer/UserInterface.cpp#L143)。

```cpp
ImGui::SliderFloat("Elevation", &renderData.rdViewElevation, -89.0f, 89.0f, "%.1f", sliderFlags);
```

这个限制是合理的，因为：

- 当 elevation 接近 `±90°` 时，前向向量接近世界上方向或下方向
- 此时 `lookAtRH(..., up = (0,1,0))` 很容易接近退化状态
- 相机在极点附近也更容易出现操作不连续

所以这里主动避开了极点。

### 4.3 当前角度约定下的直观方向

按这套公式：

- `Azimuth = 0°` 时朝 `+X`
- `Azimuth = 90°` 时朝 `+Z`
- `Azimuth = 180°` 时朝 `-X`
- `Azimuth = 270°` 时朝 `-Z`

这是一个标准右手系下、以 Y 为上轴的水平旋转定义。

## 5. View Matrix 是怎么来的

### 5.1 target 不是存储值，而是推导值

代码不是直接存一个“相机看向哪里”，而是：

```cpp
target = position + forward;
```

这意味着相机系统里真正的输入是：

- 相机位置
- 相机朝向

而不是：

- 相机位置
- 目标点

### 5.2 使用右手系 `lookAt`

view 矩阵由：

```cpp
glm::lookAtRH(eye, target, up)
```

生成。

这里明确了三件事：

- 坐标系是右手系
- 世界上方向固定是 `(0, 1, 0)`
- 没有 roll，自由度只有 yaw/pitch

这也解释了为什么这套相机不会“歪头”：  
它永远拿世界 Y 作为上方向。

## 6. Projection Matrix 是怎么来的

### 6.1 选择的是 `perspectiveRH_ZO`

代码使用：

```cpp
glm::perspectiveRH_ZO(...)
```

含义是：

- `RH`：Right-Handed，右手系
- `ZO`：Depth range Zero to One，裁剪空间深度范围是 `[0, 1]`

这和 Vulkan / D3D 风格的深度约定一致，而不是 OpenGL 传统的 `[-1, 1]`。

### 6.2 当前固定参数

除了 FOV 之外，投影参数目前都是硬编码的：

- `near = 0.1f`
- `far = 500.0f`
- `aspect = outputWidth / outputHeight`

对应代码在 [Renderer.cpp](/S:/Github/RAnimation/Samples/SkeletalAnimation/src/Renderer/Renderer.cpp#L223)。

### 6.3 FOV 是竖直视场角

`glm::perspective*` 系列函数的 FOV 参数是垂直方向视场角。

也就是说 UI 中的 `Field of View` 改的是垂直 FOV，不是水平 FOV。

UI 范围在 [UserInterface.cpp](/S:/Github/RAnimation/Samples/SkeletalAnimation/src/Renderer/UserInterface.cpp#L145)：

```cpp
ImGui::SliderInt("Field of View", &renderData.rdFieldOfView, 40, 150, "%d", sliderFlags);
```

## 7. UI 参数分别代表什么

相机相关 UI 位于 [UserInterface.cpp](/S:/Github/RAnimation/Samples/SkeletalAnimation/src/Renderer/UserInterface.cpp#L143)。

### 7.1 `Field of View`

- 类型：`int`
- 默认值：`60`
- UI 范围：`40 ~ 150`
- 含义：垂直方向视场角

影响：

- 值小：更“长焦”，画面更窄，透视感更弱
- 值大：更“广角”，画面更宽，透视感更强

### 7.2 `Azimuth`

- 类型：`float`
- 默认值：`330`
- UI 范围：`0 ~ 360`
- 含义：绕世界 Y 轴的水平朝向角

影响：

- 改变相机“向哪边看”
- 不改变相机位置

### 7.3 `Elevation`

- 类型：`float`
- 默认值：`-20`
- UI 范围：`-89 ~ 89`
- 含义：俯仰角

影响：

- 正值：往上看
- 负值：往下看

### 7.4 `Position`

- 类型：`glm::vec3`
- 默认值：`(2, 5, 7)`
- UI 范围：每轴 `-50 ~ 50`
- 含义：相机世界坐标

影响：

- 改变观察点位置
- 不直接改变角度

## 8. 导入模型后的自动对焦

### 8.1 触发条件

导入模型后，相机不会每次都自动跳转，只会在“场景里原本没有模型”的情况下自动对焦。

代码在 [Renderer.cpp](/S:/Github/RAnimation/Samples/SkeletalAnimation/src/Renderer/Renderer.cpp#L668)：

```cpp
const bool shouldFocusImportedModel = mModelInstData.miModelList.empty();
```

如果这是第一份模型，`AddModel()` 成功导入并创建默认实例后，会执行：

```cpp
const glm::mat4 worldTransform = instance->GetWorldTransformMatrix();
focusCameraOnPoint(glm::vec3(worldTransform[3]));
```

见 [Renderer.cpp](/S:/Github/RAnimation/Samples/SkeletalAnimation/src/Renderer/Renderer.cpp#L691)。

### 8.2 自动对焦算法

实现位于 [Renderer.cpp](/S:/Github/RAnimation/Samples/SkeletalAnimation/src/Renderer/Renderer.cpp#L1373)。

逻辑是：

1. 先给模型一个默认观察偏移：

```cpp
const glm::vec3 defaultViewOffset(2.0f, 5.0f, 7.0f);
cameraPosition = focusPoint + defaultViewOffset;
```

2. 再从 `focusPoint - cameraPosition` 反推当前朝向：

```cpp
viewDirection = normalize(focusPoint - cameraPosition);
elevation = degrees(asin(clamp(viewDirection.y, -1, 1)));
azimuth = degrees(atan2(viewDirection.z, viewDirection.x));
if (azimuth < 0) azimuth += 360;
```

这个设计的好处是：

- 不需要用户手动猜角度
- 位置和角度是匹配的
- 导入第一个模型后能立刻看到目标

它的限制也很明显：

- 偏移是固定常量，不看包围盒大小
- 大模型、小模型、超长模型都会用同一套 `(2,5,7)` 偏移
- 本质上是“看向实例原点”，不是“看向模型最佳 framing”

## 9. 之前“模型整体上下颠倒”为什么是相机问题

这个问题很关键，因为它很容易被误判成：

- glTF 轴系转换错了
- 动画 clip 全是倒着的
- 骨骼矩阵顺序错了

实际根因在投影矩阵阶段。

之前 `Renderer.cpp` 里在生成 `perspectiveRH_ZO` 之后，又额外做了一次：

```cpp
projectionMatrix[1][1] *= -1.0f;
```

这会把整个 scene 在屏幕空间上下翻转。

由于这一步发生在 camera/projection 阶段，所以现象会表现为：

- 所有 clip 都倒着
- 静态与动画对象都会一起倒
- UI 仍然正常

修掉这行之后，模型恢复正常。  
所以这个问题不是 `glm` 不能用于 Vulkan，而是已经用了合适的投影函数之后，又多做了一次手动翻转。

## 10. GLM 在这里到底扮演什么角色

GLM 只是数学库，不是“OpenGL 专用相机库”。

这个 sample 的做法是显式选择适合当前图形 API 约定的接口，而不是依赖默认行为：

- `glm::lookAtRH(...)`
- `glm::perspectiveRH_ZO(...)`

这两个选择已经把约定说清楚了：

- 右手系
- 深度范围 `[0, 1]`

所以这里不需要把问题归因到“GLM 默认是 OpenGL 风格”。  
真正重要的是：

- 你选了哪个 `lookAt`
- 你选了哪个 `perspective`
- 你之后有没有再额外改矩阵

## 11. 这套相机的局限

当前实现适合 sample 演示，但不适合直接扩展成完整编辑器相机。

主要限制如下。

### 11.1 没有真正的 orbit 中心

虽然导入模型时会对焦到某个点，但系统本身不保存“当前 pivot / target”。

所以用户改完位置和角度之后，相机只是单纯地：

- 在某个位置
- 看某个方向

而不是围绕某个目标稳定旋转。

### 11.2 没有 roll

上方向永远固定 `(0,1,0)`，这让控制简单，但也限制了自由度。

### 11.3 near/far plane 固定

当前没有根据场景尺度动态调整裁剪面：

- 太小的 near plane 会增加深度精度压力
- 太远的 far plane 会放大 z fighting 风险

### 11.4 自动对焦不看包围盒

大部分 sample 级模型能用，但对极端尺寸的模型 framing 不稳定。

## 12. 排查相机问题时该先看什么

如果你怀疑“模型看不见”是相机问题，建议按这个顺序排查。

### 12.1 先看 position / azimuth / elevation 是否匹配

最常见的问题不是矩阵坏了，而是：

- 相机位置在这里
- 相机朝向却没看向模型

### 12.2 再看 RenderDoc 中 VS 输出是否进入裁剪空间

如果 `SV_Position` 在 VS 后已经全部跑出视锥，那么问题通常在：

- world/view/projection 变换链
- 参数约定
- 手动矩阵修正

而不是 light。

### 12.3 如果所有 clip 都统一倒置，优先看 projection

这是一个强信号。

单个 clip 姿态异常，更像动画数据；  
所有 clip 一起上下翻转，更像 camera/projection 链路。

## 13. 可以怎么继续改进

如果后面要把这套相机继续做成更稳定的调试工具，建议按下面顺序增强。

### 13.1 保存显式 `focus target`

把当前系统从“位置 + 角度”升级成：

- `target`
- `distance`
- `azimuth`
- `elevation`

这样才是更完整的 orbit camera。

### 13.2 基于包围盒自动 framing

导入模型时根据 AABB 或 bounding sphere 计算：

- 最佳距离
- 最佳近远裁剪面

而不是固定 `(2,5,7)`。

### 13.3 区分调试相机和展示相机

当前相机同时承担：

- 调试观察
- 首次导入模型自动 framing

这两个目标未必一致，后面可以拆开。

## 14. 一句话总结

SkeletalAnimation 当前的相机系统是：

- 一个基于 `position + azimuth + elevation` 的右手系 look-at 相机
- 每帧在 CPU 端生成 `forward -> target -> view -> projection`
- 使用 `glm::lookAtRH` 与 `glm::perspectiveRH_ZO`
- 导入首个模型时会自动对焦到实例原点

它的实现简单、可控、适合 sample，但也因为简单，任何额外的矩阵修正都会直接作用到整个 scene。之前“所有动画都倒着”就是这个典型案例。
