# Output Contract

本文件定义每类任务的标准输出文件和落盘位置。自然语言请求自由，但产物结构必须稳定。

## Workspace 标准目录

每个非系统 workspace 使用以下目录结构：

```text
<workspace>\
├─ WORKSPACE.md
├─ TOPICS.md
├─ STATUS.md
├─ wiki\
├─ prompts\
├─ answers\
├─ reviews\
├─ cases\
└─ artifacts\
```

## `prepare` 的标准输出

### 必须更新

- `WORKSPACE.md`
- `TOPICS.md`
- `STATUS.md`

### 默认生成

- `wiki/index.md`
- `wiki/roadmap.md`

### 可选生成

- `prompts/<workspace-prefix>-B01-index.md`
- 第一批单题文件

## `analyze-and-generate` 的标准输出

### 专题类内容

- 放入 `wiki/`
- 文件名使用主题 slug
- 例如：
  - `wiki/binding-flow.md`
  - `wiki/render-loop.md`
  - `wiki/descriptor-lifecycle.md`

### Bug / 诊断 / 案例类内容

- 放入 `cases/`
- 例如：
  - `cases/device-lost-diagnosis.md`
  - `cases/imgui-streamer-lifecycle.md`

## `generate-prompts` 的标准输出

### 批次索引

- `prompts/<prefix>-Bxx-index.md`

### 单题文件

- `prompts/<prefix>-Bxx-Qyy-<slug>.md`

### 题目文件必须包含

- 题目目标
- 作答要求
- 推荐参考文档
- 回答文件路径
- 若需要手推/日志/截图，必须写明 `artifacts/` 放置要求

## `review` 的标准输出

### 单题批改

- `reviews/<id>.md`

### 单题批改文档必须包含

- 结论
- 分项评分
- 主要问题
- 证据与纠正
- 下一步建议

## `review-batch` 的标准输出

### 每题单独批改

- `reviews/<id>.md`

### 批次汇总

- `reviews/<prefix>-Bxx-summary.md`

### 状态同步

- 更新 `STATUS.md`

## `refresh-status` 的标准输出

- 更新 `STATUS.md`

### `STATUS.md` 最少包含

- 当前已完成主题
- 已完成题目批次
- 主要薄弱点
- 下一步建议

## `sync-comprehensive` 的标准输出

- `_Comprehensive/wiki/<topic>.md`
- 或 `_Comprehensive/cases/<topic>.md`
- 同步内容必须注明来源 workspace

## 不应发生的行为

- 不覆盖用户写好的 `answers/*.md`
- 不在 `answers/` 目录中写入批改结果
- 不把专题文档直接写到 `_System`
- 不把 sample 专属知识混写到错误的 workspace
