# Defaults

本文件定义当用户自然语言请求中省略细节时的默认行为。

## 默认 Workspace 解释

- 如果用户明确提到 sample 名：
  - 路由到对应 sample workspace
- 如果用户说“所有 Samples”“整个仓库”“综合”：
  - 路由到 `_Comprehensive`
- 如果用户说“修改流程”“修改规范”“修改批改方式”：
  - 路由到 `_System`

## 默认“相关内容”含义

当用户说：

- “准备相关内容”
- “给我整理一套材料”
- “搭一套学习包”

默认等价于生成核心学习包：

- `wiki/index.md`
- `wiki/roadmap.md`
- 更新 `TOPICS.md`
- 更新 `STATUS.md`

必要时附带第一批验证题。

## 默认题目模式

- 默认先出核心版，不直接覆盖全仓库所有细节
- 核心版应优先覆盖：
  - 启动路径
  - 主循环
  - 关键数据结构
  - 资源生命周期
  - 调试案例

## 默认批改范围

当用户只说“帮我批改下”但没有指定题号时：

- 在目标 workspace 中扫描 `answers/`
- 优先批改：
  - 存在答案文件
  - 且尚无同名 `reviews/` 文件
  - 且匹配到题目文件

## 默认配套材料处理

- 若 `artifacts/<id>/` 存在：
  - 视为该题配套材料，纳入批改
- 若不存在：
  - 仅按回答文档批改
- 若用户说“回答文档和手推放置好了”：
  - 默认扫描 `artifacts/<id>/`

## 默认“所有 Samples”处理

- 当前仓库只有 `SkeletalAnimation`
- 因此对“所有 Samples”的任何执行：
  - 仍然写入 `_Comprehensive`
  - 但必须显式注明当前实际分析范围只有 `SkeletalAnimation`

## 默认状态同步

执行以下任务后默认更新 `STATUS.md`：

- `prepare`
- `generate-prompts`
- `review-batch`
- `refresh-status`

## 默认语言

- 协议文档、学习文档、题目、批改默认使用中文
- 若需要保留英文标识符、源码路径、函数名，应原样保留
