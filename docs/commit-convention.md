# 提交规范 (Commit Convention)

本仓库遵循 [Conventional Commits](https://www.conventionalcommits.org/) 规范，
并在此基础上约定更明确的标题、作用域和正文写法，方便后续排查
`layout` / `overview` / `canvas` / `lane` / `stack` 等路径上的行为变化。

## 提交消息结构

推荐结构如下：

```text
<type>(<scope>)<!?>: <summary>

<body>

<trailers>
```

示例：

```text
fix(layout): restore portrait fullscreen geometry on insert

1. 🐛 问题现象 (Symptoms)
- 竖屏模式下 fullscreen 后再创建窗口，新窗口会继承错误的整屏高度。

2. 🔍 根本原因 (Root Cause)
- 新窗口插入 active stack 时继承了 expanded window 的逻辑高度。

3. 🛠️ 解决方案 (Solution)
- 在插入路径里先恢复 expanded 几何，再让新窗口继承恢复后的尺寸。

4. ⚠️ 影响与注意事项 (Impact/Notes)
- 已验证 cmake --build Debug -j 和 ctest --test-dir Debug --output-on-failure。
```

## 标题要求

标题格式：

```text
<type>(<scope>)<!?>: <summary>
```

约束如下：

- `type` 使用小写英文。
- `scope` 推荐始终填写，使用简短名词，推荐 `kebab-case`。
- `summary` 使用命令式语气，描述“这次提交做了什么”，不要写成过去式。
- `summary` 首字母通常小写，专有名词除外。
- `summary` 不要以句号结尾。
- 标题尽量控制在 72 个字符以内。

## 类型说明

| 类型 | 适用场景 | 示例 |
| --- | --- | --- |
| `build` | 构建系统、编译参数、依赖、打包流程调整 | `build(cmake): link spdlog header-only target explicitly` |
| `ci` | CI 配置、工作流、自动化检查 | `ci(actions): run logic tests on push` |
| `docs` | 仅文档修改 | `docs(readme): clarify smoke test workflow` |
| `feat` | 新功能 | `feat(overview): add workspace selection preview` |
| `fix` | 缺陷修复 | `fix(layout): restore portrait fullscreen geometry on insert` |
| `perf` | 性能优化 | `perf(canvas): reduce relayout work during focus changes` |
| `refactor` | 不改变外部行为的重构 | `refactor(layout): flatten nested control flow` |
| `style` | 不改变语义的格式或样式调整 | `style(core): normalize include ordering` |
| `test` | 测试增补或修正 | `test(layout): cover portrait fullscreen insertion regression` |
| `chore` | 杂项维护，不属于上述分类 | `chore(repo): refresh editorconfig defaults` |
| `revert` | 回退已有提交 | `revert(layout): restore previous monitor handoff logic` |
| `bump` | 版本或发行号升级 | `bump(release): prepare v0.4.0` |

## 常用作用域

建议优先使用能直接对应仓库模块的作用域：

| 作用域 | 适用模块 |
| --- | --- |
| `layout` | 横跨 `canvas` / `lane` / `stack` 的布局行为修改 |
| `canvas` | `src/layout/canvas/*` |
| `lane` | `src/layout/lane/*` |
| `stack` | `src/model/stack.*` 或 stack 级逻辑 |
| `overview` | `src/overview/*` |
| `dispatch` | dispatcher、命令入口、参数解析 |
| `core` | 公共数学、方向、共享 helper |
| `tests` | `tests/*` 或测试基建 |
| `docs` | 文档目录、README |
| `build` | `CMakeLists.txt`、`Makefile`、打包相关 |

如果改动横跨多个模块，但本质上是同一条布局行为链路，优先使用 `layout`，
而不是把标题写得过长。

## 正文要求

### 简单提交

对于纯文档、小范围拼写修正、机械性改名等轻量提交，允许只写标题，
或补一小段简短正文。例如：

```text
docs(readme): clarify fullscreen behavior

Document that scroller fullscreen is different from Hyprland native fullscreen.
```

### 非平凡代码提交

对于影响行为、调试路径较长、跨多个文件的代码改动，推荐使用下面的四段正文模板。
这也是本仓库当前更推荐的提交写法。

```text
1. 🐛 问题现象 (Symptoms)
- 描述用户可观察到的错误、限制或回归现象。

2. 🔍 根本原因 (Root Cause)
- 解释真正导致问题的状态传播、控制流或数据结构原因。

3. 🛠️ 解决方案 (Solution)
- 说明本次提交改了哪些关键路径、为什么这样改。

4. ⚠️ 影响与注意事项 (Impact/Notes)
- 说明行为边界、兼容性、未覆盖范围和验证命令。
```

正文要求如下：

- 每一节都应围绕当前提交本身，不写泛泛背景。
- `Solution` 只写本次提交实际做了什么，不写未来计划。
- `Impact/Notes` 里优先写验证方式，例如 `cmake --build Debug -j`、`ctest --test-dir Debug --output-on-failure`。
- 如果存在未纳入提交的工作区改动，可以在 `Impact/Notes` 中显式说明。

## 破坏性变更

如果提交引入不兼容修改：

- 在标题中使用 `!`，例如 `refactor(dispatch)!: rename fullscreen dispatcher arguments`
- 并在正文或脚注中明确写出 `BREAKING CHANGE:`

示例：

```text
refactor(dispatch)!: rename fullscreen dispatcher arguments

BREAKING CHANGE: `scroller:togglefullscreen` no longer accepts legacy aliases.
```

## Trailer 约定

如有必要，可在正文末尾追加 trailer：

- `BREAKING CHANGE:` 标记破坏性变更
- `Refs:` 关联 issue 或提交
- `Made-with:` 记录辅助工具来源

示例：

```text
Refs: #42
Made-with: Codex
```

## 校验规则

基础标题校验可使用：

```regex
^(build|ci|docs|feat|fix|perf|refactor|style|test|chore|revert|bump)(\([a-z0-9][a-z0-9-]*\))?(!)?: [^\n\r]+$
```

仓库内更推荐：

- 代码提交默认填写 `scope`
- 非平凡改动默认补充结构化正文

## 最佳实践

1. 一个提交只做一件事

- 不要把 bugfix、重构、格式化和文档更新混进同一个提交，除非它们不可分割。

2. 标题写“动作”，不要写“结果感想”

- 好例子：`fix(layout): restore portrait fullscreen geometry on insert`
- 差例子：`fix(layout): fullscreen bug fixed`

3. 正文写“因果链”，不要只写改了哪些文件

- 优先解释为什么会出错、为什么这样修，而不是罗列文件名。

4. 验证信息要具体

- 写清实际跑过的命令，而不是只写“tested”或“verified”。

5. 与仓库术语保持一致

- 优先使用仓库已有术语，如 `row` / `column`、`lane`、`stack`、`overview`、`fullscreen`、`expanded`。

## 常见错误

错误示例：

```text
update fullscreen bug
fix:missing space
Fix(layout): Use wrong capitalized type
docs(wrong scope): invalid scope with spaces
refactor(layout): refactored layout code
```

推荐写法：

```text
fix(layout): restore portrait fullscreen geometry on insert
docs(commit): document repository commit convention
refactor(overview): split monitor orientation spaces
```
