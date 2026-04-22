# Feature / Debug Workflow

这份文档把最近两次 `overview` 相关工作的真实排障链路整理成一套可复用流程：

- 一次是“预览窗口被错误放缩/裁切”的渲染问题
- 一次是“overview 里的 `movefocus` 不按视觉上下关系走”的导航问题

目标不是记录一次性的细节，而是把以后做 feature 和 debug 时应该遵循的步骤固定下来。

## 适用范围

这套流程默认适用于：

- `src/overview/*`
- `src/layout/canvas/*`
- `src/layout/lane/*`
- `src/model/stack.*`
- 任何需要同时验证“纯逻辑 + Hyprland 运行态 + 真正渲染结果”的修改

## 核心原则

1. 先保护主会话，再开始排障

- 默认优先用 nested `Hyprland`
- 不直接在用户的主会话里反复热加载、切换 workspace、试错式下命令
- 所有 nested 操作尽量显式带 `hyprctl -i <instance>`

2. 先把现象固定下来，再改代码

- 先写清“用户看到了什么”和“预期应该是什么”
- 在没有稳定复现之前，不要急着猜是 render、model 还是 dispatcher 问题

3. 先缩小问题层级，再选择修复点

- 先判断是渲染层、overview 模型层、导航评分层，还是运行态副作用层
- 不要一上来同时改多层逻辑

4. 手工复现一旦稳定，就尽快固化成脚本

- 如果问题需要重复观察，优先写 `scripts/repro-*.sh`
- 脚本应该输出：
  - nested instance
  - log 路径
  - result 路径或关键结论

5. 修复必须同时留下“纯测试”和“运行态复现”中的至少一种

- 纯数学/评分/路由问题，优先补 `tests/*`
- 依赖 compositor 行为的问题，至少补 nested repro 脚本
- 最稳妥的是两者都补

## 标准流程

### 1. 定义问题

先记录四件事：

- 可观察现象
- 预期行为
- 触发条件
- 明确不该碰的东西

例如：

- “overview 里窗口预览被放大裁切”
- “`movefocus u` 应该落到视觉上方的预览，而不是另一张卡片里的对角目标”
- “不要直接控制主会话，只能在 nested Hyprland 里复现”

### 2. 建立最小 nested 复现

优先复用已有脚本，而不是从零开始手动点环境：

- 预览渲染类问题：[`scripts/repro-overview.sh`](../scripts/repro-overview.sh)
- overview 导航类问题：[`scripts/repro-overview-movefocus.sh`](../scripts/repro-overview-movefocus.sh)
- overview 跨屏 accept 类问题：[`scripts/repro-overview-accept-cross-monitor.sh`](../scripts/repro-overview-accept-cross-monitor.sh)

如果已有脚本不够，就在它基础上改，不要新造完全不同的启动方式。

脚本设计要求：

- 用最小配置启动 nested `Hyprland`
- 自动加载 `./Debug/hyprscroller.so`
- 明确窗口拓扑和 dispatcher 序列
- 默认自动退出，避免残留测试会话
- 输出 run dir、log、result

### 3. 确定失败层级

把问题先归到下面其中一层：

1. Render / preview 层

- 典型症状：
  - 缩放错误
  - 裁切错误
  - 只在 snapshot fallback 或 live surface fallback 下出错
  - 视觉结果不对，但 selection / accept 逻辑是对的

2. Overview model / scene 层

- 典型症状：
  - 渲染看到的几何关系和导航/选中不一致
  - workspace 卡片位置对，但 target 位置对不上
  - synthetic target 出现位置异常

3. Navigation / scoring 层

- 典型症状：
  - `movefocus` 落到错误目标
  - 候选目标存在，但排序不符合视觉预期
  - 同列/同行目标被对角线目标抢走

4. Runtime / dispatcher / side-effects 层

- 典型症状：
  - 命令发给了错误实例
  - `toggleoverview accept`/`cancel` 跳错 workspace
  - focus 在 overview 打开期间被真实改变

### 4. 记录运行态证据

优先收这些证据：

- `hyprctl instances -j`
- `hyprctl -i <instance> clients -j`
- `hyprctl -i <instance> workspaces -j`
- `hyprctl -i <instance> activeworkspace -j`
- `~/.hyprland/plugins/hyprscroller/hyprscroller.log`

原则：

- 记录“当时的事实”，不要用印象替代快照
- 明确区分主实例和 nested 实例
- 如果日志不够，先在正确层加日志，再继续猜

### 5. 只在正确层修复

修复前先回答：

- 这个问题是“坐标源不一致”还是“评分规则不对”？
- 这个问题应该在 render、model、logic 还是 session 层收口？
- 改完后别的层能不能直接复用，不需要再补第二套逻辑？

优先选择：

- 把坐标统一到单一来源
- 把评分规则写成纯 helper
- 把会变的运行态操作留在 `effects.cpp` / dispatcher 层

避免：

- 为了修一个 UI 症状，在多个层重复做几何换算
- 让 scene 和 navigation 各自维护一套不同的 box 语义

### 6. 把经验固化成测试或脚本

最少要留下下面之一：

- `tests/*` 里的纯逻辑回归测试
- `scripts/repro-*.sh` 里的稳定 nested 复现场景

推荐顺序：

1. 先补脚本，先稳定复现
2. 再把核心选择/评分逻辑抽纯，补 `tests/*`
3. 修复后再用脚本跑通真实行为

### 7. 三层验证

每次这类改动默认按三层验证：

1. 构建层

```bash
cmake --build Debug -j
```

2. 纯逻辑测试层

```bash
ctest --test-dir Debug --output-on-failure
```

3. nested 运行态层

```bash
./scripts/repro-overview.sh --outer-monitor <monitor>
./scripts/repro-overview-movefocus.sh --outer-monitor <monitor>
```

如果改动只影响其中一条链路，至少也要跑对应的 repro 脚本。

### 8. 最后再提交

提交前确认：

- worktree 里没有混入无关改动
- 文档、脚本、测试和代码修改在同一条因果链上
- commit message 里写清：
  - 症状
  - 根因
  - 解决方案
  - 验证命令

## 案例 A：overview 预览放缩 / 裁切修复

对应提交：

- `9c09a4b fix(overview): render off-screen previews without cropped zoom`

### 现象

- 某些 overview 预览会被莫名放大
- 某些 xdg-shell 窗口会出现过度裁切
- 问题主要出现在窗口部分离开 monitor、snapshot fallback 不再可靠时

### 复现方式

优先使用：

```bash
./scripts/repro-overview.sh --outer-monitor HDMI-A-1
```

这个脚本会：

- 在 nested `Hyprland` 里创建 overview 场景
- 构造图片预览窗口和多个终端窗口
- 自动打开 `scroller:toggleoverview`
- 让 preview 渲染路径稳定可观察

### 如何缩小到正确层

这次问题最终被归到 render / preview 层，而不是 overview selection 层。

关键信号是：

- 错的是窗口画面本身
- 不是选中框跳错，也不是 accept 跳错
- 某些窗口在 framebuffer snapshot 路径正常，fallback 到 live surface tree 路径时异常

### 最终修复点

修复主要落在 [`src/overview/render/draw.cpp`](../src/overview/render/draw.cpp)：

- 对完全在 monitor 内的窗口，优先走 snapshot 路径
- 对离开 monitor 的窗口，回退到 live `wl_surface` 树
- `preview_surface_source_box()` 改为使用 `window->getWindowMainSurfaceBox()`
- `calculateUVForSurface()` 使用 source surface 的真实尺寸，而不是 overview 缩小后的目标框

对应代码位置：

- [`preview_surface_source_box`](../src/overview/render/draw.cpp)
- [`draw_window_snapshot`](../src/overview/render/draw.cpp)
- [`draw_window_surface_tree`](../src/overview/render/draw.cpp)

### 这次案例沉淀出的规则

- 视觉缩放问题先看 UV / source box / fallback path，不要先怀疑 overview selection
- overview 的目标框变小，不代表 source surface 也该按那个尺寸算 UV
- 只要 snapshot 依赖 monitor framebuffer，就必须检查窗口是否完整位于 monitor 内

## 案例 B：overview movefocus 导航修复

对应提交：

- `b58b6a2 fix(overview): follow rendered geometry in movefocus`

### 现象

- overview 中执行 `scroller:movefocus u/d/l/r` 时，偶尔会跳到视觉上并不是邻居的目标
- 在两张 workspace 卡片并排、卡片内部再投影窗口预览的场景里，`movefocus` 会跳到另一张卡片里的对角线窗口

### 复现方式

使用：

```bash
./scripts/repro-overview-movefocus.sh --outer-monitor HDMI-A-1
```

这个脚本会稳定构造：

- `workspace 1`：两个 `column` 模式窗口
- `workspace 2`：三个 `column` 模式窗口
- 从 `workspace 1` 的下方窗口打开 overview
- 执行一次 `movefocus u`
- 再执行一次无参 `toggleoverview`，接受当前 overview 选择并输出最终落点

旧 bug 下，它会跳到 `workspace 2 / ws2-middle`。

### 如何缩小到正确层

这次问题最终被拆成两层：

1. overview model / scene 坐标语义

- render 里看到的是 preview 投影后的 box
- navigation 最初使用的却是原始 snapshot box

2. navigation / scoring 规则

- 即便 box 统一之后，只按主轴中心距离评分仍会让对角线目标抢走同列/同行候选

### 最终修复点

第一步是统一几何来源：

- [`src/overview/model.cpp`](../src/overview/model.cpp)
  在 workspace grid 布局完成后，把 target box 重写成最终 preview 几何
- [`src/overview/scene.cpp`](../src/overview/scene.cpp)
  直接复用 model 里的 preview box，不再重新投影一遍
- [`src/overview/scene/layout.cpp`](../src/overview/scene/layout.cpp)
  提供共享的 workspace content box 和 global projection helper

第二步是修正导航评分：

- [`src/overview/logic.cpp`](../src/overview/logic.cpp)
  在 `pickTargetIndex()` 里先优先选择和当前 target 存在 directional beam overlap 的候选，
  再比较主轴距离、同屏优先级和次轴距离

### 对应测试

- 纯逻辑回归：
  [`tests/overview_logic_tests.cpp`](../tests/overview_logic_tests.cpp)
- preview 投影回归：
  [`tests/overview_scene_tests.cpp`](../tests/overview_scene_tests.cpp)

### 这次案例沉淀出的规则

- 只要用户说“看起来上下关系不对”，先检查 navigation 和 render 是否共用同一套几何
- overview 里的方向导航不应该只看中心点主轴距离
- 视觉导航默认应优先 beam overlap，再考虑距离
- 手工复现一旦稳定，立刻升级成 nested repro 脚本

## 以后默认怎么做

以后在这个仓库里做 debug 或 feature，默认按下面顺序走：

1. 先用 nested `Hyprland` 建立最小复现
2. 先确认失败发生在 render、model、logic 还是 runtime 层
3. 用日志和 `hyprctl -i <instance>` 记录事实
4. 把手工复现固化成 `scripts/repro-*.sh`
5. 在正确层做最小修复
6. 能抽纯的逻辑就补 `tests/*`
7. 至少跑一遍 build、一遍 logic tests、一遍 nested repro
8. 最后再按提交规范提交

如果某次工作跳过了这些步骤，应该把理由写清楚，而不是默认省略。
