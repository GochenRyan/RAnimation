# Task Protocol

本文件定义自然语言请求如何被解释成固定的学习任务，并规定每类任务的标准执行动作。

## 总原则

- 用户使用自然语言提出任务。
- 执行时不要求用户提供结构化命令。
- 模型必须先判断：
  - 任务类型
  - 目标 workspace
  - 默认输出位置
  - 是否需要扫描已有答案或资料

## 任务类型

### `prepare`

- 典型表达：
  - “帮我准备好 `SkeletalAnimation` 的相关内容”
  - “给我准备一套学习材料”
- 目标：
  - 为某个 workspace 建立初始学习包
- 标准动作：
  - 检查或创建该 workspace 的 `WORKSPACE.md`、`TOPICS.md`、`STATUS.md`
  - 生成或更新：
    - `wiki/index.md`
    - `wiki/roadmap.md`
  - 需要时生成第一批题目

### `analyze-and-generate`

- 典型表达：
  - “检查所有 Samples 的 binding 流程，生成 binding 的专题内容”
  - “分析 `SkeletalAnimation` 的渲染循环并整理成专题”
- 目标：
  - 对源码做专题分析，并产出可学习文档
- 标准动作：
  - 扫描相关源码
  - 生成或更新主题文档到 `wiki/` 或 `cases/`
  - 主题文档标题与文件名必须稳定、可复用

### `generate-prompts`

- 典型表达：
  - “给 `SkeletalAnimation` 出一批验证题”
  - “针对 binding 专题生成题目”
- 目标：
  - 生成验证掌握程度的题目
- 标准动作：
  - 生成题目批次索引
  - 生成单题文件
  - 指定回答文档和配套材料的放置路径

### `review`

- 典型表达：
  - “我的 `SkeletalAnimation` 的回答文档和手推放置好了，帮我批改下”
  - “帮我批改 binding 的专题作答”
- 目标：
  - 对已有答案和配套材料进行批改
- 标准动作：
  - 扫描 `answers/`
  - 扫描 `artifacts/`
  - 按命名规则匹配题目、答案、手推、日志等材料
  - 在 `reviews/` 中生成对应批改文档

### `review-batch`

- 典型表达：
  - “把这一批都批改一下”
  - “检查这个 workspace 下所有还没批改的回答”
- 目标：
  - 批量批改
- 标准动作：
  - 识别未批改条目
  - 逐项写入 `reviews/`
  - 汇总本批次常见问题到 `STATUS.md`

### `refresh-status`

- 典型表达：
  - “更新一下 `SkeletalAnimation` 的学习进度”
  - “给我一个当前掌握情况总结”
- 目标：
  - 更新学习状态和下一步建议
- 标准动作：
  - 读取 `wiki/`、`prompts/`、`answers/`、`reviews/`
  - 更新 `STATUS.md`

### `sync-comprehensive`

- 典型表达：
  - “把 `SkeletalAnimation` 的结论同步到综合目录”
  - “做一个仓库级总结”
- 目标：
  - 把 sample 级结论提升到仓库级
- 标准动作：
  - 从 sample workspace 提取稳定结论
  - 更新 `_Comprehensive/wiki/` 或 `_Comprehensive/cases/`

## 解释优先级

### 优先判断目标对象

- 若请求中点名 sample 名：
  - 优先 sample workspace
- 若请求中出现“所有 samples”“整个仓库”“综合”：
  - 优先 `_Comprehensive`
- 若请求中要求调整规则、协议、工作方式：
  - 优先 `_System`

### 再判断动作

- “准备”“搭建”“整理一套” -> `prepare`
- “检查”“分析”“生成专题” -> `analyze-and-generate`
- “出题”“验证”“考我” -> `generate-prompts`
- “批改”“检查答案”“帮我看手推” -> `review` 或 `review-batch`
- “总结进度”“当前掌握情况” -> `refresh-status`

## 执行约束

- 不覆盖用户已经写好的 `answers/`、`artifacts/`
- 批改结果必须写到 `reviews/`，而不是覆盖答案原文
- 主题文档和题目文件名必须稳定，避免同一主题反复改名
- 对“所有 Samples”的请求，当前仓库只有 `SkeletalAnimation` 时必须显式说明
