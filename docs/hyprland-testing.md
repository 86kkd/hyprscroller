# Hyprland 测试说明

这份文档说明如何用 `Hyprland` 本身来做 `hyprscroller` 的集成测试，
重点覆盖以下场景：

- 在不污染当前桌面会话的前提下复现插件问题
- 用 debug 构建的插件做手工回归
- 采集 `Hyprland` 和 `hyprscroller` 的日志
- 对 `layout` 切换、`overview`、跨 workspace/monitor 行为做压测

## 目标

推荐优先使用“嵌套 Hyprland”方式测试，而不是直接在你正在使用的主会话里反复热加载。

这样做的好处：

- 插件崩溃时不会把你当前桌面一起带崩
- 可以单独抓一份 `Hyprland` stdout/stderr
- 可以明确区分测试实例和主实例

## 1. 构建 Debug 插件

先在仓库根目录构建 debug 版本：

```bash
make debug
```

构建完成后，测试用插件路径是：

```bash
./Debug/hyprscroller.so
```

## 2. 启动嵌套 Hyprland 测试实例

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
Hyprland -c /tmp/hyprscroller-test.conf
```

说明：

- 这个命令会在你当前 Wayland 会话里再起一个 Hyprland
- 它适合做插件 debug
- 不建议拿它替代日常登录会话

## 3. 找到测试实例

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

## 4. 在测试实例里创建窗口

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

## 5. 手工测试 layout 切换

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

如果你要做循环压测，可以直接用 shell：

```bash
for i in $(seq 1 100); do
    hyprctl -i <instance-signature> keyword general:layout master >/dev/null || break
    hyprctl -i <instance-signature> keyword general:layout scroller >/dev/null || break
done
```

## 6. 手工测试 overview

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

接受或取消：

```bash
hyprctl -i <instance-signature> dispatch scroller:toggleoverview accept
hyprctl -i <instance-signature> dispatch scroller:canceloverview
```

推荐重点回归：

- 单 workspace overview 是否铺满 monitor
- 多 workspace 时横屏/竖屏网格是否合理
- 打开/关闭 overview 时是否有卡片残留
- overview 期间真实 focus 是否被错误改变

## 7. 手工测试 scroller dispatchers

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

## 8. 日志采集

### 8.1 Hyprland 日志

如果你是直接在终端里运行：

```bash
Hyprland -c /tmp/hyprscroller-test.conf
```

那么这个终端里的 stdout/stderr 就是测试实例的 `Hyprland` 日志。

如果要落盘：

```bash
Hyprland -c /tmp/hyprscroller-test.conf > /tmp/hyprland-test.log 2>&1
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

## 9. 截图和录屏

如果你想确认 overview 或布局渲染结果，可以在嵌套实例上截图。

例如测试实例的 socket 是 `wayland-2`：

```bash
WAYLAND_DISPLAY=wayland-2 grim /tmp/hyprscroller-test.png
```

这样可以在不影响主会话的情况下抓到测试实例的画面。

## 10. 清理测试实例

测试结束后，直接让测试实例退出：

```bash
hyprctl -i <instance-signature> dispatch exit
```

不要直接对主实例下发这个命令。

## 11. 推荐测试流程

推荐顺序：

1. `make debug`
2. 启动嵌套 `Hyprland -c /tmp/hyprscroller-test.conf`
3. 用 `hyprctl instances -j` 找到测试实例
4. 在测试实例里创建 `kitty`
5. 跑目标场景
6. 失败时同时保存：
   - `Hyprland` 终端日志
   - `~/.hyprland/plugins/hyprscroller/hyprscroller.log`
   - 必要时的截图
7. 用 `hyprctl -i <instance-signature> dispatch exit` 退出测试实例

## 12. 建议记录的复现信息

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

## 13. 关联文档

- 手工回归清单见 [smoke-test-checklist.md](./smoke-test-checklist.md)
- 提交规范见 [commit-convention.md](./commit-convention.md)
