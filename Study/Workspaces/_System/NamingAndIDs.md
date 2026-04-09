# Naming and IDs

本文件定义题目、答案、手推材料、批改结果的命名规则。目标是让自然语言工作流下的自动匹配稳定可靠。

## Workspace 前缀

- `SkeletalAnimation` -> `SA`
- `_Comprehensive` -> `COMP`

## 批次 ID

- 批次格式：`Bxx`
- 例如：
  - `B01`
  - `B02`

## 题目 ID

- 单题格式：
  - `<prefix>-Bxx-Qyy-<slug>`
- 例如：
  - `SA-B01-Q01-startup-path`
  - `SA-B01-Q02-render-loop`
  - `COMP-B01-Q01-binding-flow`

## 文件命名规则

### 题目批次索引

- `prompts/<prefix>-Bxx-index.md`

### 单题文件

- `prompts/<prefix>-Bxx-Qyy-<slug>.md`

### 回答文件

- `answers/<prefix>-Bxx-Qyy-<slug>.md`

### 批改文件

- `reviews/<prefix>-Bxx-Qyy-<slug>.md`

### 配套材料目录

- `artifacts/<prefix>-Bxx-Qyy-<slug>/`

## 配套材料建议命名

在 `artifacts/<id>/` 下使用以下约定：

- `hand-derivation.md`
- `notes.md`
- `log.txt`
- `trace.md`
- `page-01.png`
- `page-02.png`

## 主题文档命名

### Wiki 文档

- `wiki/<slug>.md`
- 例如：
  - `wiki/index.md`
  - `wiki/roadmap.md`
  - `wiki/binding-flow.md`

### Case 文档

- `cases/<slug>.md`
- 例如：
  - `cases/device-lost-diagnosis.md`

## 自动匹配规则

当用户说“回答文档和手推放好了，帮我批改”时：

1. 先扫描 `answers/`
2. 读取每个回答文件名中的 `<id>`
3. 在 `artifacts/<id>/` 下查找配套材料
4. 在 `reviews/` 中生成同名批改文件

## 不推荐的写法

- 不要把一整批题目写进一个回答文件
- 不要把手推放在 `answers/`
- 不要手动修改同一题目的 `<id>`
- 不要使用中文空格和不稳定标题做文件主键
