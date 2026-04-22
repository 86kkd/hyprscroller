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

对于下面这些轻量改动，允许只写标题，或补一小段简短正文：

- 小范围文档勘误
- 纯格式化或注释风格调整
- 机械性重命名
- 不影响行为的仓库杂项维护

例如：

```text
docs(readme): clarify fullscreen behavior

Document that scroller fullscreen is different from Hyprland native fullscreen.
```

### 按类型选正文模板

非平凡提交默认补结构化正文，但不同 `type` 应该选不同模板，不要把所有提交都写成 bugfix 报告。

### `build` 型提交

适用于构建系统、编译参数、依赖解析、打包路径和安装流程调整。

推荐模板：

```text
1. 🔧 调整目标 (Goal)
- 说明为什么要改构建或依赖路径，例如编译失败、链接方式不稳、打包产物不一致。

2. 🧱 构建变更 (Build Changes)
- 说明改了哪些 CMake / Makefile / 依赖声明 / 安装路径 / 打包逻辑。

3. ✅ 兼容性与验证 (Compatibility/Verification)
- 说明验证了哪些构建命令、平台、配置或产物行为。

4. ⚠️ 影响与注意事项 (Impact/Notes)
- 说明是否影响开发环境、安装方式、缓存、产物位置或后续升级路径。
```

示例：

```text
build(cmake): link spdlog header-only target explicitly

1. 🔧 调整目标 (Goal)
- 当前链接路径依赖环境差异，某些机器上会出现 spdlog 解析不稳定的问题。

2. 🧱 构建变更 (Build Changes)
- 在 CMake 中显式链接 spdlog 的 header-only target，避免隐式传播依赖。

3. ✅ 兼容性与验证 (Compatibility/Verification)
- 已验证 `cmake -S . -B Debug -DCMAKE_BUILD_TYPE=Debug` 和 `cmake --build Debug -j`。

4. ⚠️ 影响与注意事项 (Impact/Notes)
- 只调整构建图，不改变运行态行为。
```

### `ci` 型提交

适用于 GitHub Actions、CI 门禁、自动化发布和静态检查流程调整。

推荐模板：

```text
1. 🎯 流程目标 (Workflow Goal)
- 说明这次 CI 调整想补什么信号、减少什么漏检或修复什么流程缺口。

2. 🤖 工作流调整 (Workflow Changes)
- 说明新增、删除或修改了哪些 job、trigger、缓存、artifact 或矩阵配置。

3. ✅ 门禁与反馈 (Checks/Signals)
- 说明现在 PR / push 会多出什么检查信号，或哪些失败模式会更早暴露。

4. ⚠️ 影响与注意事项 (Impact/Notes)
- 说明耗时变化、缓存影响、分支策略要求或需要同步更新的仓库约定。
```

示例：

```text
ci(actions): run logic tests on push

1. 🎯 流程目标 (Workflow Goal)
- 当前逻辑测试只在手工环境里跑，push 后缺少基础回归信号。

2. 🤖 工作流调整 (Workflow Changes)
- 为 GitHub Actions 增加构建和 `ctest` job，并在 push 事件上触发。

3. ✅ 门禁与反馈 (Checks/Signals)
- 之后 push 会直接反馈逻辑层回归，不需要等到后续手工验证阶段才发现失败。

4. ⚠️ 影响与注意事项 (Impact/Notes)
- 新增 job 会增加 CI 时长，但不改变本地开发命令。
```

### `docs` 型提交

对于 `docs` 提交，小范围勘误、改名、补一句说明时可以只写标题；
但如果是新增文档、补完整流程、引入新的团队约定，推荐使用结构化正文。

推荐模板：

```text
1. 📚 文档目的 (Purpose)
- 说明这份文档面向谁，解决什么信息缺口。

2. 📝 新增 / 调整内容 (Content Added/Changed)
- 说明新增了哪些章节、示例、流程或约束。

3. 👀 读者影响 (Reader Impact)
- 说明读者会如何改变操作方式、排障顺序或协作习惯。

4. ⚠️ 影响与注意事项 (Impact/Notes)
- 说明文档依赖的前提、适用范围、关联脚本或代码路径。
```

示例：

```text
docs(readme): clarify smoke test workflow

1. 📚 文档目的 (Purpose)
- README 里对状态型布局改动的验证入口说明不够明显。

2. 📝 新增 / 调整内容 (Content Added/Changed)
- 在开发者章节补充 smoke test checklist 和 Hyprland testing notes 的入口链接。

3. 👀 读者影响 (Reader Impact)
- 新读者可以更快找到运行态验证流程，而不是只停留在纯逻辑测试。

4. ⚠️ 影响与注意事项 (Impact/Notes)
- 这次只调整文档索引，不改任何测试脚本本身。
```

### `feat` 型提交

对于 `feat` 提交，不要勉强套用 “Symptoms / Root Cause”。
新增能力更适合写“目标 -> 用户变化 -> 实现 -> 验证”。

推荐模板：

```text
1. 🎯 目标与动机 (Goal/Motivation)
- 说明这次新增能力解决了什么使用场景、限制或长期缺口。

2. ✨ 用户可见变化 (User-Facing Change)
- 描述用户现在能做什么，或现有行为如何被扩展。

3. 🛠️ 实现摘要 (Implementation)
- 说明引入了哪些核心结构、入口、约束或兼容策略。

4. ⚠️ 影响与注意事项 (Impact/Notes)
- 说明验证命令、行为边界、已知限制和未覆盖范围。
```

示例：

```text
feat(overview): add workspace selection preview

1. 🎯 目标与动机 (Goal/Motivation)
- overview 目前只能看窗口分布，缺少 workspace 级选择线索。

2. ✨ 用户可见变化 (User-Facing Change)
- 打开 overview 时可以看到 workspace 级预览和选中状态。

3. 🛠️ 实现摘要 (Implementation)
- 为 overview scene 增加 workspace 预览数据和对应渲染路径，保留现有 dispatcher 不变。

4. ⚠️ 影响与注意事项 (Impact/Notes)
- 已验证逻辑测试和对应 nested Hyprland 场景。
```

### `fix` 型提交

对于 `fix`、带明显回归链路的 `perf`、以及以排障为主的 `test` 提交，
推荐使用下面这套四段正文模板。

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

适合这类正文的标题示例：

```text
fix(layout): restore portrait fullscreen geometry on insert
```

### `perf` 型提交

适用于不改变预期外部语义、但显著改善耗时、分配、重绘次数或工作量的提交。

推荐模板：

```text
1. 📉 性能目标 (Performance Target)
- 说明这次优化针对的热路径、慢路径、卡顿点或冗余工作。

2. ⚙️ 优化策略 (Optimization)
- 说明减少了哪些重复计算、无效重排、分配、渲染或事件风暴。

3. ✅ 行为保持方式 (Behavior Safety)
- 说明哪些用户可见语义必须保持不变，以及如何避免把 perf 提交写成功能改动。

4. ⚠️ 影响与注意事项 (Impact/Notes)
- 说明验证方式、观测场景、收益边界和未覆盖路径。
```

示例：

```text
perf(canvas): reduce relayout work during focus changes

1. 📉 性能目标 (Performance Target)
- focus 高频切换时会重复触发整 canvas relayout，导致热路径开销偏高。

2. ⚙️ 优化策略 (Optimization)
- 将 focus 更新收敛到窗口级或 lane 级重排，避免无变化时的整画布重算。

3. ✅ 行为保持方式 (Behavior Safety)
- 不改变 focus 结果和窗口几何语义，只减少重复工作。

4. ⚠️ 影响与注意事项 (Impact/Notes)
- 已验证常用 dispatcher 路径和对应逻辑测试。
```

### `refactor` 型提交

对于 `refactor` 提交，正文重点不是“修了什么 bug”，而是：

- 为什么现在的结构值得调整
- 调整后职责如何变化
- 如何证明外部行为没有被意外改坏

推荐模板：

```text
1. 🎯 重构动机 (Motivation)
- 说明当前结构的问题，例如职责耦合、重复逻辑、难以测试或阅读路径过深。

2. 🧱 结构调整 (Structural Changes)
- 说明这次拆分、提炼、合并或迁移了哪些核心模块和边界。

3. ✅ 行为保持方式 (Behavior Safety)
- 说明哪些公共接口、运行态行为或测试预期保持不变。

4. ⚠️ 影响与注意事项 (Impact/Notes)
- 说明验证命令、迁移边界、后续可继续清理但本次未做的内容。
```

示例：

```text
refactor(layout): split canvas restore logic from target callbacks

1. 🎯 重构动机 (Motivation)
- canvas 生命周期、target callback 和 restore 逻辑混在一起，导致恢复路径难以单独推理。

2. 🧱 结构调整 (Structural Changes)
- 将 snapshot capture/restore 与普通 new/remove target 流程拆开，保留原有 dispatcher 入口不变。

3. ✅ 行为保持方式 (Behavior Safety)
- 不改变现有 dispatcher 名称和 layout 命令语义，只重排内部职责并保留原有测试覆盖。

4. ⚠️ 影响与注意事项 (Impact/Notes)
- 已验证 `cmake --build Debug -j` 和 `ctest --test-dir Debug --output-on-failure`。
```

### `style` 型提交

适用于不改变语义的格式、空白、注释样式、include 排序和代码风格统一。

```text
1. 🎨 调整范围 (Formatting Scope)
- 说明这次样式整理覆盖哪些模块、文件或规则。

2. 🧹 样式变更 (Style Changes)
- 说明统一了哪些格式、排序、命名或注释风格。

3. ✅ 语义不变说明 (Semantic Safety)
- 明确说明这次提交不应改变编译结果或运行态行为。

4. ⚠️ 影响与注意事项 (Impact/Notes)
- 说明是否依赖格式化工具、是否建议 review 时忽略 whitespace 变化。
```

示例：

```text
style(core): normalize include ordering

1. 🎨 调整范围 (Formatting Scope)
- 本次只整理公共 helper 文件里的 include 顺序和空行分组。

2. 🧹 样式变更 (Style Changes)
- 按仓库约定统一系统头、第三方头和项目头的分组顺序。

3. ✅ 语义不变说明 (Semantic Safety)
- 不修改任何控制流、数据结构或对外接口。

4. ⚠️ 影响与注意事项 (Impact/Notes)
- review 时可以按 whitespace-only 思路阅读这次提交。
```

### `test` 型提交

适用于测试增补、测试基建修正、回归样例固化和 repro 脚本新增。

推荐模板：

```text
1. 🧪 覆盖目标 (Coverage Goal)
- 说明这次测试想保护什么行为、回归或使用场景。

2. 📝 测试内容 (Tests Added/Changed)
- 说明新增了哪些逻辑测试、集成脚本、fixture 或断言。

3. ✅ 保护的行为 (Behavior Guarded)
- 说明这些测试失败时通常意味着什么行为被打破。

4. ⚠️ 影响与注意事项 (Impact/Notes)
- 说明运行命令、运行前提、依赖环境和未覆盖边界。
```

示例：

```text
test(layout): cover portrait fullscreen insertion regression

1. 🧪 覆盖目标 (Coverage Goal)
- 保护 fullscreen 后插入新窗口时的几何恢复行为，避免旧回归再次出现。

2. 📝 测试内容 (Tests Added/Changed)
- 为对应布局链路补充逻辑测试和可重复的 nested repro 场景。

3. ✅ 保护的行为 (Behavior Guarded)
- 如果测试失败，说明 fullscreen 插入路径再次出现错误尺寸传播。

4. ⚠️ 影响与注意事项 (Impact/Notes)
- 逻辑测试可直接本地运行，nested repro 依赖 Hyprland 会话。
```

### `chore` 型提交

适用于仓库杂项维护、辅助脚本清理、配置同步和不归类到其他类型的维护性工作。

推荐模板：

```text
1. 🧰 维护目标 (Maintenance Goal)
- 说明这次维护是为了减少什么摩擦、统一什么约定或清理什么历史残留。

2. 🔧 维护内容 (Changes)
- 说明改了哪些配置、脚本、仓库元信息或开发辅助文件。

3. 👀 开发流程影响 (Developer Impact)
- 说明这次改动会如何影响日常开发、review、发布或本地工具使用。

4. ⚠️ 影响与注意事项 (Impact/Notes)
- 说明是否需要同步更新本地环境、缓存、生成文件或团队约定。
```

示例：

```text
chore(repo): refresh editorconfig defaults

1. 🧰 维护目标 (Maintenance Goal)
- 仓库当前格式约定分散在多个工具里，editorconfig 默认值已经落后于现状。

2. 🔧 维护内容 (Changes)
- 更新 editorconfig 中的缩进、行尾和空白规则，使其与现有代码风格一致。

3. 👀 开发流程影响 (Developer Impact)
- 新文件会更容易遵循统一样式，减少 review 里的格式噪音。

4. ⚠️ 影响与注意事项 (Impact/Notes)
- 这次提交不主动重排现有文件，只更新默认规则。
```

### `revert` 型提交

适用于明确回退某个提交或某条错误方向的修改，不应写成普通 `fix`。

推荐模板：

```text
1. ⏪ 回退对象 (What Is Reverted)
- 说明回退了哪条提交、哪段逻辑或哪次错误尝试。

2. 🔍 回退原因 (Why Revert)
- 说明触发回退的回归、兼容性问题或错误假设。

3. 🧱 回退范围 (Rollback Scope)
- 说明是完整回退还是部分回退，以及哪些后续改动被保留。

4. ⚠️ 影响与注意事项 (Impact/Notes)
- 说明回退后的已知状态、后续计划和验证命令。
```

示例：

```text
revert(layout): restore previous monitor handoff logic

1. ⏪ 回退对象 (What Is Reverted)
- 回退最近一次跨显示器 handoff 选择逻辑调整。

2. 🔍 回退原因 (Why Revert)
- 新逻辑在特殊 workspace 下引入了错误 focus 落点。

3. 🧱 回退范围 (Rollback Scope)
- 只回退 handoff 选择路径，保留同批提交里的日志增强。

4. ⚠️ 影响与注意事项 (Impact/Notes)
- 回退后行为恢复到上一版稳定状态，后续会重新设计这条路由逻辑。
```

### `bump` 型提交

适用于版本号、发行号、插件版本声明和发布元信息升级。

推荐模板：

```text
1. ⬆️ 升级目标 (Upgrade Target)
- 说明这次 bump 的版本目标和触发原因。

2. 📦 版本变更 (Version Changes)
- 说明具体改了哪些版本号、标签、发行文件或元数据。

3. ✅ 兼容性 / 发布影响 (Compatibility/Release Impact)
- 说明这次 bump 对安装、升级、发布流程或用户预期的影响。

4. ⚠️ 影响与注意事项 (Impact/Notes)
- 说明是否需要同步 tag、release note、打包元数据或文档。
```

示例：

```text
bump(release): prepare v0.4.0

1. ⬆️ 升级目标 (Upgrade Target)
- 为下一次公开发布准备版本元数据。

2. 📦 版本变更 (Version Changes)
- 更新插件版本声明和相关打包文件中的 release 标识。

3. ✅ 兼容性 / 发布影响 (Compatibility/Release Impact)
- 这次只调整版本元数据，不引入新的运行态行为变化。

4. ⚠️ 影响与注意事项 (Impact/Notes)
- 发布时仍需同步 tag、release note 和打包清单。
```

正文通用要求如下：

- 先判断提交类型，再选最贴近的模板，不要所有提交都强行写成 bugfix 报告。
- 每一节都应围绕当前提交本身，不写泛泛背景。
- `Solution` / `Implementation` / `Structural Changes` / `Changes` 只写本次提交实际做了什么，不写未来计划。
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

3. 正文写“决策链”，不要只写改了哪些文件

- `build` 解释构建目标、构建变更和兼容性验证。
- `ci` 解释流程目标、工作流调整和新增门禁信号。
- `docs` 解释文档目的、内容变化和读者影响。
- `fix` 解释现象、根因和修复路径。
- `feat` 解释目标、用户变化和实现方式。
- `perf` 解释性能目标、优化策略和行为保持方式。
- `refactor` 解释重构动机、结构调整和行为保持方式。
- `style` 解释格式范围、样式变更和语义不变说明。
- `test` 解释覆盖目标、测试内容和保护的行为。
- `chore` 解释维护目标、维护内容和开发流程影响。
- `revert` 解释回退对象、回退原因和回退范围。
- `bump` 解释升级目标、版本变更和发布影响。

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
