# Feature / Debug Workflow

这份文档把最近两次 `overview` 相关工作的真实排障链路整理成一套可复用流程：

- 一次是“预览窗口被错误放缩/裁切”的渲染问题
- 一次是“overview 里的 `movefocus` 不按视觉上下关系走”的导航问题

目标不是记录一次性的细节，而是把以后做 feature 和 debug 时应该遵循的步骤固定下来。

需要先说明一点：

- 这份文档最初来源于两次真实排障，所以前半部分天然更偏 debug 语境
- 但仓库里的大量改动并不是“先有 bug 再修”，而是“先定义能力，再补验证”

因此下面把流程明确拆成两条：

1. 新 feature 开发流程
2. debug / 回归排障流程

## 适用范围

这套流程默认适用于：

- `src/overview/*`
- `src/layout/canvas/*`
- `src/layout/lane/*`
- `src/model/stack.*`
- 任何需要同时验证“纯逻辑 + Hyprland 运行态 + 真正渲染结果”的修改

## 核心原则

1. 先保护主会话，再开始开发或排障

- 默认优先用 nested `Hyprland`
- 不直接在用户的主会话里反复热加载、切换 workspace、试错式下命令
- 所有 nested 操作尽量显式带 `hyprctl -i <instance>`

2. 新 feature 先定义 contract，再写代码

- 先写清楚“这次要新增什么能力”
- 先写清楚“哪些已有行为必须保持不变”
- 对状态型 feature，先写清楚状态属于 window / stack / lane / canvas / workspace / session 的哪一层

3. debug 先把现象固定下来，再改代码

- 先写清“用户看到了什么”和“预期应该是什么”
- 在没有稳定复现之前，不要急着猜是 render、model 还是 dispatcher 问题

4. 先缩小问题层级，再选择实现点或修复点

- 先判断是渲染层、overview 模型层、导航评分层，还是运行态副作用层
- 不要一上来同时改多层逻辑

5. 手工复现或手工 feature 场景一旦稳定，就尽快固化成脚本

- 如果问题需要重复观察，优先写 `scripts/repro-*.sh`
- 如果 feature 需要反复验证状态流转，也优先写 `scripts/repro-*.sh`
- 脚本应该输出：
  - nested instance
  - log 路径
  - result 路径或关键结论

6. 新增能力或修复都必须留下“纯测试”和“运行态复现”中的至少一种

- 纯数学/评分/路由问题，优先补 `tests/*`
- 依赖 compositor 行为的问题，至少补 nested repro 脚本
- 最稳妥的是两者都补

## 新 Feature 标准流程

### 1. 先定义 feature contract

开始编码前，先把下面几件事写清楚：

- 用户要得到什么新能力
- 明确的触发入口是什么
  - 新 dispatcher
  - 现有 dispatcher 行为扩展
  - workspace / layout 生命周期钩子
  - overview / render 表现变化
- 明确不做什么
- 哪些已有行为绝对不能被顺手改掉

推荐至少写清下面这几个问题：

- 这个 feature 是用户可见行为，还是纯内部结构准备？
- 它的“成功标准”是什么？
- 它的“失败表现”会长什么样？
- 它会影响哪些模块：
  - `overview`
  - `canvas`
  - `lane`
  - `stack`
  - dispatcher / session / render

### 2. 先确定状态归属和生命周期

这一步对 layout 类 feature 尤其重要。

先回答：

- 状态属于哪一层：
  - `Window`
  - `Stack`
  - `Lane`
  - `CanvasLayout`
  - `Workspace`
  - session / runtime repository
- 状态要活多久：
  - 一次 dispatcher 调用内
  - 当前 lane / stack 生命周期内
  - workspace 切换后还在
  - layout 切换后还在
  - plugin reload 后还在
  - 整个 Hyprland 重启后还在
- 状态丢失时应该怎么 fallback

如果这里不先写清楚，后面很容易出现：

- 把短生命周期状态放到过高层，导致脏状态泄漏
- 把长生命周期状态放到过低层，导致 layout switch / reload 后丢失
- 多个层同时维护一份语义相近但不同步的状态

### 3. 先选一个最小的纵向切片

不要一上来把所有边界情况一起做完。

推荐先选一个最小但真实的用户路径：

- 一个入口
- 一个核心状态流转
- 一个可观察结果

例如：

- “增加一个 overview 里的新选择行为”
- “让 scroller 在 layout switch 后恢复当前 workspace 布局”
- “让某个 dispatcher 在 column 模式下新增一种尺寸策略”

最小纵向切片的目标不是“功能做完”，而是：

- 先证明设计方向是对的
- 先证明状态归属没有选错
- 先得到一个可以写测试和脚本的稳定行为

### 4. 在动代码前先设计验证面

先决定这个 feature 最终如何被证明“真的完成了”。

默认至少考虑三类验证：

1. 纯逻辑测试

- 适合纯选择、评分、状态转换、序列化 / 反序列化

2. nested 运行态脚本

- 适合 layout switch
- 适合 plugin reload
- 适合 workspace / monitor / focus / fullscreen / overview 交互

3. 文档或配置入口

- 新 dispatcher 是否要写入 README
- 新 workflow 是否要写进 docs
- 新脚本是否要写进 testing docs

对 stateful feature，建议额外补一个“状态边界表”：

- 正常路径是否生效
- layout switch 后是否还生效
- plugin reload 后是否还生效
- 多 workspace / special workspace 下是否还生效

### 5. 用 nested `Hyprland` 建立最小 feature 场景

即便不是 debug，也不要默认只靠脑补和 unit test。

优先做下面几件事：

- 复用已有 `scripts/repro-*.sh`
- 如果现有脚本不够，在其基础上扩展
- 如果 feature 本身就是一个新的状态链路，就补一个新的 repro 脚本

脚本要求尽量和 debug 场景一致：

- 最小配置启动 nested `Hyprland`
- 自动加载 `./Debug/hyprscroller.so`
- 明确窗口拓扑和 dispatcher 序列
- 输出 run dir、log、result
- 默认自动退出

### 6. 在正确层做最小实现

实现前先问：

- 这个能力应该在哪一层拥有唯一语义？
- 哪一层应该只消费结果，而不是重复推导？
- 哪些旧路径只是需要接入，不应该重新定义一套语义？

优先选择：

- 让状态只有一个主拥有者
- 让公共逻辑能抽成 helper 或 snapshot / repository
- 让 runtime 副作用仍然留在 dispatcher / effects / lifecycle 入口层

避免：

- 为了赶 feature，把同一语义在 `scene` 和 `logic` 各写一遍
- 同时改 render、model、dispatcher，却没有明确主语义源
- 先铺很多 edge case，但主路径还没跑通

### 7. 先跑通主路径，再补边界

主路径通了之后，再系统补边界：

- 多 workspace
- special workspace
- 多 monitor
- fullscreen / maximize
- overview 打开期间
- layout switch / plugin reload
- 缺失状态或部分状态恢复失败时的 fallback

如果某个边界会显著改变设计，回到前面的生命周期和状态归属重新确认，不要硬补 patch。

### 8. 把 feature 经验固化成测试、脚本和文档

一个新 feature 完成后，至少要留下下面几类资产中的两类，最好三类都有：

- `tests/*` 里的纯逻辑测试
- `scripts/repro-*.sh` 里的 nested 运行态脚本
- `docs/*` 或 `README.md` 里的使用 / 验证说明

对用户可见入口，优先补文档：

- 新 dispatcher
- 新配置项
- 新的验证脚本
- 新的开发约定

### 9. 三层验证

新 feature 默认也按三层验证：

1. 构建层

```bash
cmake --build Debug -j
```

2. 纯逻辑测试层

```bash
ctest --test-dir Debug --output-on-failure
```

3. nested 运行态层

- 跑与这个 feature 对应的 `scripts/repro-*.sh`
- 如果没有现成脚本，先补脚本再验证

### 10. 最后再提交

提交前确认：

- 新功能的主路径和边界路径都已经被验证
- 文档、脚本、测试和代码修改在同一条功能链上
- commit message 与提交类型匹配
  - `feat`
  - `refactor`
  - `docs`
  - 其他合适的 type
- 正文说明清楚：
  - 目标 / 动机
  - 用户可见变化
  - 实现摘要
  - 验证命令

## Debug / 回归标准流程

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

以后在这个仓库里，先判断你做的是哪一类工作：

### 如果是新 feature

默认按下面顺序走：

1. 先定义 feature contract
2. 先确定状态归属和生命周期
3. 先选最小纵向切片
4. 先设计测试和 nested 验证面
5. 用 nested `Hyprland` 跑通主路径
6. 再补边界情况
7. 固化成测试、脚本和文档
8. 最后再按提交规范提交

### 如果是 debug / 回归修复

默认按下面顺序走：

1. 先用 nested `Hyprland` 建立最小复现
2. 先确认失败发生在 render、model、logic 还是 runtime 层
3. 用日志和 `hyprctl -i <instance>` 记录事实
4. 把手工复现固化成 `scripts/repro-*.sh`
5. 在正确层做最小修复
6. 能抽纯的逻辑就补 `tests/*`
7. 至少跑一遍 build、一遍 logic tests、一遍 nested repro
8. 最后再按提交规范提交

如果某次工作跳过了这些步骤，应该把理由写清楚，而不是默认省略。
