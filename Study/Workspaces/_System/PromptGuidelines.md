# Prompt Guidelines

本文件定义自然语言请求如何更稳定地触发正确的任务路由与产物生成。

## 1. 是否必须包含关键词

不必须。

你可以直接用自然语言发需求，系统会先按以下顺序解释：

1. 识别对象范围（sample 级 / 仓库级 / 协议级）
2. 识别动作类型（prepare / analyze-and-generate / generate-prompts / review）
3. 识别主题（例如 GPU、binding、sync、streamer）
4. 按 Output Contract 落盘

对应协议：

- `WorkspaceRegistry.md`
- `TaskProtocol.md`
- `OutputContract.md`
- `Defaults.md`

## 2. 建议在请求中至少给出的 3 个信息

为了提高可控性，建议你的自然语言里至少包含：

- 对象范围：`SkeletalAnimation` 或 `所有 Samples` 或 `整个仓库`
- 动作：`准备` / `分析` / `生成专题` / `出题` / `批改`
- 产出类型：`wiki` / `cases` / `prompts` / `reviews`

## 3. 推荐表达模板

可直接复制改字：

- `在 <workspace> 里，分析 <topic>，生成专题内容，放到 wiki，并给一批验证题。`
- `检查 <scope> 的 <topic> 流程，输出专题到 cases，附带排查清单。`
- `我的 <workspace> 回答和手推放好了，按题号批改并更新 STATUS。`

## 4. 关键词与默认动作映射

- `准备`、`整理一套`、`相关内容` -> `prepare`
- `检查`、`分析`、`专题`、`流程` -> `analyze-and-generate`
- `出题`、`验证`、`考我` -> `generate-prompts`
- `批改`、`检查答案`、`看手推` -> `review` / `review-batch`

主题词示例：

- `GPU`、`pipeline`、`barrier`、`queue`、`fence`、`semaphore` -> 渲染/同步专题
- `binding`、`descriptor`、`layout` -> binding 专题
- `imgui`、`streamer` -> UI 上传与生命周期专题

## 5. 关于“生成 GPU 专题内容”的默认路由

当你只说“生成 GPU 的专题内容”：

- 默认路由到 `_Comprehensive`
- 默认产物路径：`_Comprehensive/wiki/gpu-topic.md`（文件名可按具体主题细化）
- 若你同时点名 `SkeletalAnimation`，则路由到该 sample workspace

## 6. 让结果更精确的加料项（可选）

你可以按需补充以下信息：

- 深度：`核心版` / `完整版`
- 视角：`偏源码调用链` / `偏诊断实战` / `偏概念`
- 附加产物：`顺带生成 6 题` / `给复盘模板`
- 限制：`只看 Samples` / `只看 Renderer + NRI`

## 7. 示例

### 示例 A：sample 级专题

`在 SkeletalAnimation 里分析 GPU 同步链路，生成专题到 wiki，顺带出 4 道验证题。`

### 示例 B：仓库级专题

`检查所有 Samples 的 binding 流程，生成 binding 专题内容，放在 _Comprehensive/wiki。`

### 示例 C：批改

`我的 SkeletalAnimation 的 SA-B01 回答和手推放置好了，按命名规则批改并更新 STATUS。`
