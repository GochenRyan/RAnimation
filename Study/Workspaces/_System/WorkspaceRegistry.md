# Workspace Registry

本目录定义 `Study/Workspaces` 下各 workspace 的职责、源码范围、默认产物位置和任务路由规则。

## 根目录

- 学习工作区根目录：`S:\Github\RAnimation\Study\Workspaces`
- 当前仓库中的 sample 根目录：`S:\Github\RAnimation\Samples`

## 当前已注册的 Workspace

### `_System`

- 角色：协议层，不承载具体知识学习内容。
- 负责内容：
  - 任务路由规则
  - 自然语言请求解释规则
  - 文件命名规则
  - 批改标准
  - 默认行为
- 默认读写范围：
  - `S:\Github\RAnimation\Study\Workspaces\_System`

### `_Comprehensive`

- 角色：仓库级综合工作区。
- 负责内容：
  - 仓库整体结构
  - `Packages`、`Samples`、`Assets`、`CMake` 的关系
  - 跨 sample 的共性专题
  - 综合题、综合复盘、综合状态
- 默认源码范围：
  - `S:\Github\RAnimation`
- 默认读写范围：
  - `S:\Github\RAnimation\Study\Workspaces\_Comprehensive`

### `SkeletalAnimation`

- 角色：sample 级工作区。
- 对应源码：
  - `S:\Github\RAnimation\Samples\SkeletalAnimation`
- 负责内容：
  - `SkeletalAnimation` 专属 Wiki
  - 题目、答案、批改
  - 专题分析
  - Bug 复盘
- 默认读写范围：
  - `S:\Github\RAnimation\Study\Workspaces\SkeletalAnimation`

## 路由规则

### 当用户提到具体 sample 名时

- 直接路由到同名 workspace。
- 例如：
  - “帮我准备好 `SkeletalAnimation` 的相关内容”
  - “批改 `SkeletalAnimation` 的回答”

### 当用户请求仓库级、跨 sample、跨模块内容时

- 路由到 `_Comprehensive`。
- 例如：
  - “检查所有 Samples 的 binding 流程”
  - “总结整个仓库的渲染资源生命周期”

### 当用户请求修改学习协议或工作方式时

- 路由到 `_System`。
- 例如：
  - “调整批改标准”
  - “修改默认命名规则”
  - “增加新的任务类型”

## 当前仓库的现实约束

- 当前 `Samples` 目录下只有一个 sample：`SkeletalAnimation`
- 因此凡是“所有 Samples”的请求，当前执行时仍然要显式注明：
  - 本次范围实际只包含 `SkeletalAnimation`
  - 结论写在 `_Comprehensive` 中，但证据当前来自这个 sample

## 将来新增 Sample 的规则

- 若 `Samples` 下新增 `<SampleName>`：
  - 新建 `Study/Workspaces/<SampleName>`
  - 目录结构与 `SkeletalAnimation` 保持一致
  - 在本文件中追加注册项
- sample 级自然语言请求默认优先路由到同名 workspace
