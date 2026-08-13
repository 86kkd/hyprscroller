# Hyprland 测试说明

这份文档说明如何用 `Hyprland` 本身来做 `hyprscroller` 的集成测试，
重点覆盖以下场景：

- 在不污染当前桌面会话的前提下复现插件问题
- 用 debug 构建的插件做手工回归
- 采集 `Hyprland` 和 `hyprscroller` 的日志
- 对 `layout` 切换、`overview`、跨 workspace/monitor 行为做定点回归

## 目标

推荐优先使用“嵌套 Hyprland”方式测试，而不是直接在你正在使用的主会话里反复热加载。

这样做的好处：

- 插件崩溃时不会把你当前桌面一起带崩
- 可以单独抓一份 `Hyprland` stdout/stderr
- 可以明确区分测试实例和主实例

## 1. 构建 Debug 插件

Arch Linux 更新 `hyprland` 包后，先确认磁盘上的版本和当前运行中的版本一致：

```bash
Hyprland --version
hyprctl version
```

如果两条命令显示的版本或 commit 不同，先退出并重新登录 Hyprland 会话，再构建和
加载插件。Hyprland 插件 ABI 绑定到具体构建；不能把针对新头文件编译的插件加载到
尚未重启的旧 Hyprland 进程中。嵌套回测也应在主会话重启后进行，避免新旧
Aquamarine/EGL 运行库混用导致 nested compositor 在插件初始化前崩溃。

先在仓库根目录构建 debug 版本：

```bash
make debug
```

构建完成后，测试用插件路径是：

```bash
./Debug/hyprscroller.so
```

插件初始化成功后，`~/.hyprland/plugins/hyprscroller/hyprscroller.log` 中的
`hyprland_abi` 字段会记录该二进制的编译目标 commit 和依赖 ABI，可用于排查加载了
旧构建的问题。

## 2. 快速复现 overview

如果你要复现“指定 monitor 上的浮动嵌套 Hyprland + 创建窗口 + 打开
overview”这条固定链路，优先直接用仓库里的脚本：

```bash
./scripts/repro-overview.sh --outer-monitor HDMI-A-1
```

脚本会完成这些事情：

- 生成一份最小 nested `Hyprland` 配置并加载 `./Debug/hyprscroller.so`
- 把嵌套 Hyprland 作为一个浮动窗口放到指定 outer monitor
- 在 nested 实例里创建一个图片预览窗口和四个终端窗口
- 在 nested 实例内部自动执行 `scroller:toggleoverview`
- 默认等待短暂可见期后自动退出 nested 实例
- 输出 nested 实例 id、日志路径和测试结果

如果不传 `--outer-monitor`，脚本会优先选择第一块竖屏 monitor，
否则退回当前 focused monitor。

如果你想保留 nested 实例做手工观察，可以显式传：

```bash
./scripts/repro-overview.sh --outer-monitor HDMI-A-1 --keep-open
```

如果你要复现“overview 里的 `movefocus` 没按视觉上下关系走”的问题，
优先用这条更定向的脚本：

```bash
./scripts/repro-overview-movefocus.sh --outer-monitor HDMI-A-1
```

它会在 nested 会话里构造两个 `column` 模式 workspace，自动执行
`toggleoverview -> movefocus u -> accept`，并把最终落点写到 result 文件里。

如果你要复现“overview 跨 monitor accept 没有真正切到目标窗口”的问题，
优先用这条脚本：

```bash
./scripts/repro-overview-accept-cross-monitor.sh --outer-monitor HDMI-A-1
```

它会在 nested 会话里创建第二块虚拟输出，在第一块 monitor 打开 overview，
将逻辑选中项移动到第二块 monitor 的目标窗口，再执行 accept，并验证
最终 focus 是否真的落到那个目标窗口上。

如果你要验证“overview 里的 `focusmonitor` 会创建 / 选择新的 canvas workspace，
并且关闭 overview 后所有可见 monitor 一起切过去”，优先用这条脚本：

```bash
./scripts/repro-overview-canvas-workspaces.sh --outer-monitor HDMI-A-1
```

它会先验证 normal-mode `scroller:focusmonitor` 只切 monitor focus，
再在 overview 里执行 `focusmonitor r -> toggleoverview(close-accept)`，并检查
5 块 nested monitor 是否一起切到新的空白 canvas workspace，再验证可以切回原
来的 canvas。脚本会把 nested 输出摆成“上中下三个横屏，左右各一个竖屏”的十字
形拓扑，并同时断言 outer `aquamarine - WAYLAND-1..5` 预览窗口的几何都符合预期。

### 2.1 复用 5 屏布局 Helper

如果后面的集成测试也要复用这套 5 屏十字布局，不要在脚本里重复写 monitor 几何，
直接 source 这个 helper：

```bash
source "$REPO_ROOT/scripts/lib/nested-monitor-layouts.sh"
```

这份 helper 提供的入口：

- `hyprscroller_five_monitor_names`
- `hyprscroller_five_outer_titles`
- `hyprscroller_monitor_logical_geometry`
- `hyprscroller_compute_five_monitor_outer_layout`
- `hyprscroller_install_outer_output_map_time_float_rules`
- `hyprscroller_outer_event_socket_path`
- `hyprscroller_start_outer_output_event_watcher`
- `hyprscroller_wait_for_outer_output_event_watcher_ready`
- `hyprscroller_wait_for_outer_output_position_events`
- `hyprscroller_create_positioned_wayland_outputs`
- `hyprscroller_position_outer_windows`
- `hyprscroller_apply_five_monitor_cross_layout`

推荐时序是先根据 outer monitor 计算出 5 个 preview window 的几何，然后在启动
nested Hyprland 之前安装最小 map-time `float` 规则，让后续
`aquamarine - WAYLAND-*` 从 map 的第一刻就是 floating。第一个 nested output 用
和 `scripts/repro-overview.sh` 一样的 exec rule 启动：`monitor; float; size; center`。
之后订阅 outer Hyprland 的 `.socket2.sock` 事件流，watcher 收到 `openwindow`
事件后，立即按 window address 执行 `resizewindowpixel` 和 `movewindowpixel`，
并保留 `setfloating` 作为幂等兜底。创建每个 nested output 后，脚本仍会同步定位
一次，最后应用 nested monitor 的十字拓扑。

这份 helper 只安装 `float` 规则，不安装 map-time `size/move` 规则，也不调用
`hyprctl reload config-only`。事件驱动 watcher 依赖 `python3` 连接 Hyprland 的
Unix event socket。

最小复用方式：

```bash
NESTED_MONITOR_NAMES=()
OUTER_OUTPUT_TITLES=()
OUTER_OUTPUT_XS=()
OUTER_OUTPUT_YS=()
OUTER_OUTPUT_WIDTHS=()
OUTER_OUTPUT_HEIGHTS=()
OUTER_OUTPUT_EVENT_STATE="$RUN_DIR/outer-output-events.tsv"
OUTER_OUTPUT_EVENT_READY="$RUN_DIR/outer-output-events.ready"

hyprscroller_five_monitor_names NESTED_MONITOR_NAMES
hyprscroller_five_outer_titles OUTER_OUTPUT_TITLES

hyprscroller_compute_five_monitor_outer_layout \
  "$outer_monitor_json" \
  "$WINDOW_WIDTH" \
  "$WINDOW_HEIGHT" \
  96 \
  64 \
  100 \
  OUTER_OUTPUT_XS \
  OUTER_OUTPUT_YS \
  OUTER_OUTPUT_WIDTHS \
  OUTER_OUTPUT_HEIGHTS

hyprscroller_install_outer_output_map_time_float_rules OUTER_OUTPUT_TITLES

OUTER_OUTPUT_EVENT_SOCKET="$(hyprscroller_outer_event_socket_path)"
hyprscroller_start_outer_output_event_watcher \
  OUTER_OUTPUT_EVENT_WATCHER_PID \
  "$OUTER_OUTPUT_EVENT_SOCKET" \
  "$OUTER_OUTPUT_EVENT_STATE" \
  "$OUTER_OUTPUT_EVENT_READY" \
  "$OUTER_MONITOR_X" \
  "$OUTER_MONITOR_Y" \
  OUTER_OUTPUT_TITLES \
  OUTER_OUTPUT_XS \
  OUTER_OUTPUT_YS \
  OUTER_OUTPUT_WIDTHS \
  OUTER_OUTPUT_HEIGHTS
hyprscroller_wait_for_outer_output_event_watcher_ready "$OUTER_OUTPUT_EVENT_READY"

hyprscroller_create_positioned_wayland_outputs \
  "$NESTED_INSTANCE" \
  1 \
  5 \
  wait_for_nested_monitor_count \
  wait_for_outer_output_window_count \
  "$OUTER_MONITOR_X" \
  "$OUTER_MONITOR_Y" \
  OUTER_OUTPUT_TITLES \
  OUTER_OUTPUT_XS \
  OUTER_OUTPUT_YS \
  OUTER_OUTPUT_WIDTHS \
  OUTER_OUTPUT_HEIGHTS

hyprscroller_wait_for_outer_output_position_events "$OUTER_OUTPUT_EVENT_STATE" 5

hyprscroller_position_outer_windows \
  "$OUTER_MONITOR_X" \
  "$OUTER_MONITOR_Y" \
  OUTER_OUTPUT_TITLES \
  OUTER_OUTPUT_XS \
  OUTER_OUTPUT_YS \
  OUTER_OUTPUT_WIDTHS \
  OUTER_OUTPUT_HEIGHTS

hyprscroller_apply_five_monitor_cross_layout \
  "$NESTED_INSTANCE" \
  "$WINDOW_WIDTH" \
  "$WINDOW_HEIGHT"
```

这套 helper 默认生成的 nested monitor 拓扑是：

- `WAYLAND-2` 上横屏
- `WAYLAND-3` 中横屏
- `WAYLAND-5` 下横屏
- `WAYLAND-1` 左竖屏
- `WAYLAND-4` 右竖屏

如果你要验证“layout 切换 / plugin reload 之后 scroller 是否恢复原布局”，
优先用这条脚本：

```bash
./scripts/repro-layout-persistence.sh
```

它会在 nested 会话里构造一个多 lane、多窗口的 scroller 布局，然后依次验证：

- `master -> scroller`
- `dwindle -> scroller`
- `plugin unload/load -> scroller`

脚本会输出 nested instance、log 路径、run dir，以及每一步的几何对比结论。

如果你要验证 `scrollergrid` 在真实 nested Hyprland 场景下的高风险路径，
优先跑这条脚本：

```bash
./scripts/repro-grid-real-coverage.sh --plugin /path/to/hyprscroller.so
```

它会实际启动 nested Waybar，并覆盖：

- 真实 Waybar reserved area / workarea
- 竖屏 monitor transform
- grid 窗口飞出 monitor 后不遮挡 Waybar reserved strip
- grid overview 打开、导航和 accept
- grid `togglefullscreen` 和 `fitsize all`
- 多个 grid 窗口和多个 legacy Canvas 窗口混合跨屏 `movewindow`

## 3. 手工启动嵌套 Hyprland 测试实例

先写一份最小测试配置，例如 `/tmp/hyprscroller-test.conf`：

```ini
monitor = , preferred, auto, 1

plugin = /home/linda/Documents/hyprscroller/Debug/hyprscroller.so

env = XCURSOR_SIZE,24

general {
    layout = scroller
    gaps_in = 4
    gaps_out = 8
    border_size = 2
}

decoration {
    rounding = 6
}

misc {
    disable_hyprland_logo = true
    disable_splash_rendering = true
    force_default_wallpaper = 0
}

input {
    kb_layout = us
}

debug {
    disable_logs = false
}
```

然后直接启动一个嵌套测试实例：

```bash
env -u HYPRLAND_INSTANCE_SIGNATURE start-hyprland -- -c /tmp/hyprscroller-test.conf
```

说明：

- 这个命令会在你当前 Wayland 会话里再起一个 Hyprland
- `start-hyprland` 为 Hyprland 0.56+ 建立 watchdog 和独立 Wayland socket
- `env -u HYPRLAND_INSTANCE_SIGNATURE` 避免嵌套实例继承外层 Hyprland 的实例标识
- 它适合做插件 debug
- 不建议拿它替代日常登录会话

## 4. 找到测试实例

启动后，用下面的命令列出所有实例：

```bash
hyprctl instances -j
```

通常你会看到：

- 主会话的实例
- 新起的嵌套测试实例

建议记住这两个字段：

- `instance`
- `wl_socket`

后续所有命令都尽量带上：

```bash
hyprctl -i <instance-signature> ...
```

不要默认发给当前主会话。

## 5. 在测试实例里创建窗口

例如在测试实例里启动两个 `kitty`：

```bash
hyprctl -i <instance-signature> dispatch exec kitty
hyprctl -i <instance-signature> dispatch exec kitty
```

查看窗口状态：

```bash
hyprctl -i <instance-signature> clients -j
```

查看当前 workspace：

```bash
hyprctl -i <instance-signature> activeworkspace -j
```

## 6. 手工测试 layout 切换

测试 `general:layout` 切换时，不要改主配置文件，直接对测试实例下发：

```bash
hyprctl -i <instance-signature> keyword general:layout master
hyprctl -i <instance-signature> keyword general:layout dwindle
hyprctl -i <instance-signature> keyword general:layout scroller
```

常见回归点：

- `master -> scroller`
- `dwindle -> scroller`
- `scroller -> master -> scroller`
- 多 workspace 下切换后再回到隐藏 workspace
- 有 `fullscreen` / `overview` 状态时再切 layout

## 7. 手工测试 overview

打开 overview：

```bash
hyprctl -i <instance-signature> dispatch scroller:toggleoverview
```

移动 overview 里的逻辑选中项：

```bash
hyprctl -i <instance-signature> dispatch scroller:movefocus r
hyprctl -i <instance-signature> dispatch scroller:movefocus l
hyprctl -i <instance-signature> dispatch scroller:movefocus u
hyprctl -i <instance-signature> dispatch scroller:movefocus d
```

移动 overview 里的 canvas workspace focus：

```bash
hyprctl -i <instance-signature> dispatch scroller:focusmonitor r
hyprctl -i <instance-signature> dispatch scroller:focusmonitor l
hyprctl -i <instance-signature> dispatch scroller:focusmonitor u
hyprctl -i <instance-signature> dispatch scroller:focusmonitor d
```

接受或取消：

```bash
hyprctl -i <instance-signature> dispatch scroller:toggleoverview
hyprctl -i <instance-signature> dispatch scroller:canceloverview
```

推荐重点回归：

- 单 workspace overview 是否铺满 monitor
- 多 workspace 时横屏/竖屏网格是否合理
- 打开/关闭 overview 时是否有卡片残留
- overview 期间真实 focus 是否被错误改变

## 8. 手工测试 scroller dispatchers

常用命令：

```bash
hyprctl -i <instance-signature> dispatch scroller:movefocus r
hyprctl -i <instance-signature> dispatch scroller:movewindow r
hyprctl -i <instance-signature> dispatch scroller:setmode row
hyprctl -i <instance-signature> dispatch scroller:setmode col
hyprctl -i <instance-signature> dispatch scroller:togglefullscreen
hyprctl -i <instance-signature> dispatch scroller:createlane r
hyprctl -i <instance-signature> dispatch scroller:focuslane r
```

推荐测试组合：

- `row` / `column` 模式切换后再创建窗口
- `togglefullscreen` 后再 `movewindow`
- 跨 lane / 跨 monitor 的 `movefocus`
- 空 workspace / 空 lane 下的 fallback 行为

## 9. 日志采集

### 8.1 Hyprland 日志

如果你是直接在终端里运行：

```bash
env -u HYPRLAND_INSTANCE_SIGNATURE start-hyprland -- -c /tmp/hyprscroller-test.conf
```

那么这个终端里的 stdout/stderr 就是测试实例的 `Hyprland` 日志。

如果要落盘：

```bash
env -u HYPRLAND_INSTANCE_SIGNATURE start-hyprland -- -c /tmp/hyprland-test.conf > /tmp/hyprland-test.log 2>&1
```

重点关注：

- 崩溃前最后几行
- `pixman` / `render` / `hook` / `workspace` 相关报错
- layout 切换时的异常输出

### 8.2 插件日志

插件日志默认写到：

```bash
~/.hyprland/plugins/hyprscroller/hyprscroller.log
```

查看末尾日志：

```bash
tail -n 200 ~/.hyprland/plugins/hyprscroller/hyprscroller.log
```

如果要只看本次测试新增的内容，推荐先记住文件大小，再在测试后截取新增部分。

## 10. 截图和录屏

如果你想确认 overview 或布局渲染结果，可以在嵌套实例上截图。

例如测试实例的 socket 是 `wayland-2`：

```bash
WAYLAND_DISPLAY=wayland-2 grim /tmp/hyprscroller-test.png
```

这样可以在不影响主会话的情况下抓到测试实例的画面。

## 11. 清理测试实例

测试结束后，直接让测试实例退出：

```bash
hyprctl -i <instance-signature> dispatch exit
```

不要直接对主实例下发这个命令。

## 12. 推荐测试流程

推荐顺序：

1. `make debug`
2. 优先运行 `./scripts/repro-overview.sh --outer-monitor <monitor>`
3. 如果脚本不适用，再手工启动嵌套 `env -u HYPRLAND_INSTANCE_SIGNATURE start-hyprland -- -c /tmp/hyprscroller-test.conf`
4. 用 `hyprctl instances -j` 找到测试实例
5. 在测试实例里创建目标窗口
6. 跑目标场景
7. 失败时同时保存：
   - `Hyprland` 终端日志
   - `~/.hyprland/plugins/hyprscroller/hyprscroller.log`
   - 必要时的截图
8. 用 `hyprctl -i <instance-signature> dispatch exit` 退出测试实例

## 13. 建议记录的复现信息

如果某个问题需要继续排查，建议至少记录这些信息：

- 使用的 `Hyprland` 版本
- 使用的插件构建类型：`Debug` 或 `Release`
- 测试实例的配置文件
- 当前 monitor 拓扑：单屏 / 双屏，横屏 / 竖屏
- 复现步骤的 dispatcher 序列
- 是否包含：
  - `fullscreen`
  - `overview`
  - `special workspace`
  - 跨 monitor handoff

## 14. 关联文档

- 手工回归清单见 [smoke-test-checklist.md](./smoke-test-checklist.md)
- 提交规范见 [commit-convention.md](./commit-convention.md)
- 推荐工程流程见 [feature-debug-workflow.md](./feature-debug-workflow.md)

## 15. 排障记录：overview 里的空 workspace 卡片

有一次排查竖屏 monitor 上 `toggleoverview` 时多出“空 workspace 卡片”，
最后确认那不是 overview 自己创建了 phantom workspace，而是主会话里本来就
残留了一个 `special:scratchpad`。

当时的关键现象：

- `hyprctl workspaces -j` 里能看到 `special:scratchpad`
- 这个 workspace 归属到竖屏 monitor
- workspace 里只有 floating window，没有 tiled window
- overview 建模时会先过滤 floating window，再给空 workspace 补一个空 target

这会让人误以为“toggleoverview 额外生成了一个空 workspace”，但真正发生的
事情是：

- 主会话之前某次测试把窗口放进了 `special:scratchpad`
- 测试结束后没有把这个 special workspace 清掉
- overview 读取到这个 workspace 记录后，把它渲染成了空卡片

这次排障里最容易犯错的点是把下面两个状态混为一谈：

- `hyprctl monitors -j` 里的 `specialWorkspace`
- `hyprctl workspaces -j` 里存在的 `special:*` workspace 记录

它们含义不同：

- `monitor.specialWorkspace` 表示这个 monitor 当前正在显示哪个 special workspace
- `workspaces -j` 里的 `special:*` 只表示 Hyprland 会话里存在这个 workspace，
  并且当前归属到某个 monitor

所以：

- 看到 `workspaces -j` 里有 `special:scratchpad`，不代表它此刻正展开显示
- 但只要 overview 的数据源把它纳入模型，它仍然可能变成一张空卡片

建议以后遇到类似问题时按这个顺序确认：

1. `hyprctl monitors -j`，确认 special workspace 是否真的处于显示状态
2. `hyprctl workspaces -j`，确认是否有残留的 `special:*` workspace 记录
3. `hyprctl clients -j`，确认该 workspace 里是否只有 floating window
4. 如果是测试遗留状态，先清理 special workspace，再判断是否真有 overview bug
