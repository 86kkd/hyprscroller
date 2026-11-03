# Hyprscroller

[Hyprscroller](https://github.com/86kkd/hyprscroller) is a
[Hyprland](https://hyprland.org) layout plugin that creates a window layout
similar to [PaperWM](https://github.com/paperwm/PaperWM). I initialy
learned how to write a *Hyprland* plugin from [hyprslidr](https://gitlab.com/magus/hyprslidr),
which is a similar idea.

This repository is an actively maintained fork of the original
[`dawsers/hyprscroller`](https://github.com/dawsers/hyprscroller), which is now
archived.

![Intro](./videos/hyprscroller.gif)

The plugin is quite feature complete and supports gaps, borders, special
workspace, scroller-managed fullscreen expansion, overview, marks and
installation through `hyprpm`.

This fork is maintained against the Hyprland versions I actually run and test.
The focus is practical compatibility with packaged Hyprland releases first,
instead of chasing trunk continuously.

The current compatibility baseline is Arch Linux Hyprland 0.57.0. Rebuild the
plugin after every Hyprland package update, and restart the Hyprland session
before loading that new build.

## Requirements

*hyprscroller* supports the version of *Hyprland* I use, which should be the
same as the Arch Linux `hyprland` package. You can try your luck with the
latest `git` changes, but I will be slower to keep up with those, as there are
too many API changes going on upstream.

Aside from those versions, even though the feature set will be frozen for
them, *hyprscroller* also supports v0.35, v0.38.1, v0.39.1 and v0.40.0.


## Building and installing

With Hyprland installed it should be as simple as running

``` sh
# builds a shared object hyprscroller.so
make all
# installs the shared library in ~/.config/hypr/plugins
make install
```

If you are hacking on the plugin, you can also run the pure logic tests without
starting Hyprland:

``` sh
cmake -S . -B Debug -DCMAKE_BUILD_TYPE=Debug
cmake --build ./Debug -j
ctest --test-dir ./Debug --output-on-failure
```

For compositor-level regressions after touching canvas or lane hot paths, run
the manual [smoke test checklist](./docs/smoke-test-checklist.md) as well.

## For Developers

If you are trying to understand how the project works internally, start with
the newcomer-oriented [architecture guide](./docs/architecture.md).

For repository workflow and commit writing, see the
[commit convention](./docs/commit-convention.md).

The shortest useful reading path is:

1. [`src/plugin/entry.cpp`](./src/plugin/entry.cpp)
2. [`src/dispatchers.cpp`](./src/dispatchers.cpp)
3. [`src/layout/canvas/layout.h`](./src/layout/canvas/layout.h)
4. [`src/layout/canvas/core.cpp`](./src/layout/canvas/core.cpp)
5. [`src/layout/canvas/focus.cpp`](./src/layout/canvas/focus.cpp)
6. [`src/layout/canvas/commands.cpp`](./src/layout/canvas/commands.cpp)

The automated tests are now split by subsystem instead of living in one large
file:

- [`tests/core_logic_tests.cpp`](./tests/core_logic_tests.cpp)
- [`tests/layout_logic_tests.cpp`](./tests/layout_logic_tests.cpp)
- [`tests/overview_logic_tests.cpp`](./tests/overview_logic_tests.cpp)

For stateful layout changes, pair those tests with:

- [smoke test checklist](./docs/smoke-test-checklist.md)
- [Hyprland testing notes](./docs/hyprland-testing.md)

For the locally maintained Hyprland layout-switch helper script that wires
`master` / `dwindle` / `scroller` and the current scroller-specific binds, see
[examples/ChangeLayout.sh](./examples/ChangeLayout.sh).

A more automated option is to use `hyprpm`.

``` sh
hyprpm add https://github.com/86kkd/hyprscroller
# verify it installed correctly
hyprpm list
```

You can enable or disable it via `hyprpm enable hyprscroller` and
`hyprpm disable hyprscroller`.


## Configuration

Hyprland 0.57 removed the legacy `plugin = ...` config keyword. If you are not
using `hyprpm`, load the plugin from your Lua configuration:

``` lua
hl.plugin.load(os.getenv("HOME") .. "/.config/hypr/plugins/hyprscroller.so")
```

Instead, if you use `hyprpm`, it should be as simple as adding this to
your `~/.config/hypr/hyprland.conf` :

``` conf
exec-once = hyprpm reload -n
```

To turn on the layout, use

``` conf
general {
    ...

    layout = scroller

    ...
}
```


## Lua commands

Hyprland 0.57 no longer registers plugin dispatchers. The plugin exposes the
following functions under `hl.plugin.scroller` instead:

| Lua function | Description |
|--------------|-------------|
| `move_focus(direction)` | Move focus inside the layout, or move the overview selection. |
| `focus_monitor(direction)` | Move monitor focus, or select another canvas in overview. |
| `move_window(direction)` | Move the active window in the requested direction. |
| `cycle_size(step)` | Cycle the active stack size with `next` or `prev`. |
| `align_window(direction)` | Align the active stack or window. |
| `admit_window()` / `expel_window()` | Move a window into or out of an adjacent stack. |
| `set_mode(mode)` | Select `row` or `col` mode. |
| `fit_size(scope)` | Fit `active`, `visible`, `all`, `toend`, or `tobeg`. |
| `toggle_fullscreen()` | Toggle scroller-managed fullscreen expansion. |
| `create_lane(direction)` / `focus_lane(direction)` | Create or focus a lane. |
| `toggle_overview(action)` / `cancel_overview()` | Open, accept, or cancel overview. |
| `marks_add(name)` / `marks_delete(name)` | Add or delete a mark. |
| `marks_visit(name)` / `marks_reset()` | Visit a mark or clear all marks. |

## Fullscreen behavior

`scroller:togglefullscreen` is not the same as Hyprland's native
`fullscreen/fullscreenstate` dispatchers.

- In *row* mode, it expands the active column horizontally so the focused
  window becomes as wide as a single-window workspace.
- In *column* mode, it expands the active window vertically so it becomes as
  tall as a single-window workspace.
- It stays inside scroller's layout model, so focus movement and scrolling
  behavior continue to work normally.


## Modes

*Hyprscroller* works in one of two modes that can be changed at any moment.

1. *row* mode: it is the default. It creates new windows in a new column.
   `cyclesize` affects the width of the active column. `alignwindow` aligns
   the active column according to the argument received. `fitsize` fits the
   selected columns to the width of the monitor.

2. *column* mode: It creates new windows in the current column, right below the
   active window. `cyclesize` affects the height of the active window.
   `alignwindow` aligns the active window within the column, according to the
   argument received. `fitsize` fits the selected windows in the column to the
   height of the monitor.


## Window/Column Focus and Movement

If you want to use *Hyprscroller* you will need to map your key bindings from
the default `movefocus`/`movewindow` to
`scroller:movefocus`/`scroller:movewindow`. The reason is *Hyprland* doesn't
have the concept (yet) of a workspace that spans more than the space of a
monitor, and when focusing directionally, it doesn't look for windows that are
"outside" of that region. If this changes in the future, these two dispatches
may become obsolete.

`movefocus` and `movewindow` accept the following directional arguments:
`l` or `left`, `r` or `right`, `u` or `up`, `d` or `dn` or `down`, `b` or
`begin` or `beginning`, `e` or `end`. So you can focus or move windows/columns
in a direction or to the beginning or end or the row.


## Resizing

`cyclesize` accepts an argument which is either `+1`/`1`/`next`, or
`-1`/`prev`/`previous`. It cycles forward or backward through three column
widths (in *row* mode): one third, one half or two thirds of the available
width of the monitor. In *column* mode, the fractions are relative to the
height of the monitor, and are: one third, one half, two thirds or one.
However, using `resizewindow`, you can modify the width or height of
any window freely.


## Aligning

Columns are generally aligned in automatic mode, always making the active one
visible, and trying to make at least the previously focused one visible too if
it fits the viewport, if not, the one adjacent on the other side. However, you
can always align any column to the *center*, *left* or *right* of the monitor
(in *row* mode), or *up* (top), *down* (bottom) or to the *center* in *column*
mode. For example center a column for easier reading, regardless of what happens to
the other columns. As soon as you change focus or move a column, the alignment
is lost.

`alignwindow` takes a parameter: `l` or `left`, `r` or `right`, `c` or
`center` or `centre`, `u` or `up` and `d` or `down`.

To use *right* or *left* you need to be in *row* mode, and to use *up* or
*down* in *column* mode. *center* behaves differently depending on the mode.
In *row* mode it aligns the active column to the center of the monitor. In
*column* mode, it aligns the active window within its column, to a centered
position.


## Admit/Expel

You can create columns of windows using `admitwindow`. It takes the active
window and moves it to the column left of its current one, right under the
active window in that column.

To expel any window from its current column and position it in a new column to
its right, use `expelwindow`.


## Fitting the Screen

When you have a ultra-wide monitor, one in a vertical position, or the default
column widths or window heights don't fit your workflow, you can use manual
resizing, but it is sometimes slow and tricky.

`scroller:fitsize` works in two different ways, depending on the active mode.

It allows you to re-fit the columns (*row* mode) or windows (*column* mode) you
want to the screen extents. It accepts an argument related to the
columns/windows it will try to fit. The new width/height of each column/window
will be proportional to its previous width or height, relative to the other
columns or windows affected.

1. `active`: It is similar to maximize, it will fit the active column/window.
2. `visible`: All the currently fully or partially visible columns/windows will
   be resized to fit the screen.
3. `all`: All the columns in the row or windows in the column will be resized
   to fit.
4. `toend`: All the columns or windows from the focused one to the end of the
   row/column will be affected.
5. `tobeg` or `tobeginning`: All the columns/windows from the focused one to
   the beginning of the row/column will now fit the screen.


## Overview

`scroller:toggleoverview` toggles a bird's eye overlay across the visible
monitors. Overview is now a read-only navigation layer:

1. Real window geometry is left untouched while overview is open.
2. `scroller:movefocus` moves the logical overview selection instead of real focus.
3. `scroller:focusmonitor` moves the focused canvas workspace preview while overview stays open.
4. Closing overview accepts the currently focused target/canvas.
5. `scroller:toggleoverview cancel` or `scroller:canceloverview` restores the
   original workspace/window.

With hold-to-preview bindings, that means you can hold your main modifier,
open overview, use `scroller:focusmonitor` on `N/M/,/.` to change canvas
workspace focus, and release the modifier to enter the currently focused
canvas workspace.

Overview can target:

1. Existing tiled windows.
2. Existing empty workspaces.
3. Synthetic empty-workspace targets created by directional navigation.

The renderer now draws stable workspace cards instead of live window previews,
so overview should be used as a monitor/workspace/window picker, not as a mode
for editing window state in place.


## Marks

You can use *marks* to navigate to frequently used windows, regardless of
which workspace they are in (it even works for the special workspace windows).

`scroller:marksadd` adds a named mark. Use a *submap* to create bindings for
several named marks you may want to use. See the configuration example for
directions.

`scroller:marksdelete` deletes a named mark created with `scroller:marksadd`.

`scroller:marksvisit` moves the focus to a previously created mark.

`scroller:marksreset` clears all marks.

Marks reference windows, but are global, they may belong to different
workspaces, so visiting a mark may switch workspaces.

You can use any string name for a mark, for example in scripts. But they are
also very convenient to use with regular key bindings by simply using a letter
as the name. Again, see the example configuration.


## Options

*hyprscroller* currently accepts the following options:

1. `column_default_width`: determines the width of new columns in *row* mode.
Possible arguments are: `onehalf` (default), `onethird`, `twothirds`,
`maximized`, `floating` (uses the default width set by the application).

2. `focus_wrap`: determines whether focus will *wrap* when at the first or
last window of a row/column. Possible arguments are: `true`|`1` (default), or
`false`|`0`.

For example:

``` conf
plugin {
    scroller {
        column_default_width = onehalf
        focus_wrap = false
    }
}
```


## Key bindings

On Hyprland 0.57, bind the plugin Lua functions after calling
`hl.plugin.load(...)`. The callback resolves the plugin function when the key is
pressed, after asynchronous plugin loading has completed:

``` lua
hl.bind("SUPER + LEFT", function() hl.plugin.scroller.move_focus("l") end)
hl.bind("SUPER + RIGHT", function() hl.plugin.scroller.move_focus("r") end)
hl.bind("SUPER + CTRL + LEFT", function() hl.plugin.scroller.move_window("l") end)
hl.bind("SUPER + CTRL + RIGHT", function() hl.plugin.scroller.move_window("r") end)
hl.bind("SUPER + BRACKETLEFT", function() hl.plugin.scroller.set_mode("row") end)
hl.bind("SUPER + BRACKETRIGHT", function() hl.plugin.scroller.set_mode("col") end)
hl.bind("SUPER + TAB", function() hl.plugin.scroller.toggle_overview() end)
```

The repository's [ChangeLayout helper](./examples/ChangeLayout.sh) installs the
same Lua-compatible bindings dynamically when switching layouts.

### Legacy pre-0.57 configuration

The dispatcher-based examples below apply only to older Hyprland releases and
are retained as a migration reference. They do not register on Hyprland 0.57.

``` conf
# Move focus with mainMod + arrow keys
bind = $mainMod, left, scroller:movefocus, l
bind = $mainMod, right, scroller:movefocus, r
bind = $mainMod, up, scroller:movefocus, u
bind = $mainMod, down, scroller:movefocus, d
bind = $mainMod, home, scroller:movefocus, begin
bind = $mainMod, end, scroller:movefocus, end

# Movement
bind = $mainMod CTRL, left, scroller:movewindow, l
bind = $mainMod CTRL, right, scroller:movewindow, r
bind = $mainMod CTRL, up, scroller:movewindow, u
bind = $mainMod CTRL, down, scroller:movewindow, d
bind = $mainMod CTRL, home, scroller:movewindow, begin
bind = $mainMod CTRL, end, scroller:movewindow, end

# Modes
bind = $mainMod, bracketleft, scroller:setmode, row
bind = $mainMod, bracketright, scroller:setmode, col

# Sizing keys
bind = $mainMod, equal, scroller:cyclesize, next
bind = $mainMod, minus, scroller:cyclesize, prev

# Admit/Expel
bind = $mainMod, I, scroller:admitwindow,
bind = $mainMod, O, scroller:expelwindow,

# Center submap
# will switch to a submap called center
bind = $mainMod, C, submap, center
# will start a submap called "center"
submap = center
# sets repeatable binds for resizing the active window
bind = , C, scroller:alignwindow, c
bind = , C, submap, reset
bind = , right, scroller:alignwindow, r
bind = , right, submap, reset
bind = , left, scroller:alignwindow, l
bind = , left, submap, reset
bind = , up, scroller:alignwindow, u
bind = , up, submap, reset
bind = , down, scroller:alignwindow, d
bind = , down, submap, reset
# use reset to go back to the global submap
bind = , escape, submap, reset
# will reset the submap, meaning end the current one and return to the global one
submap = reset

# Resize submap
# will switch to a submap called resize
bind = $mainMod SHIFT, R, submap, resize
# will start a submap called "resize"
submap = resize
# sets repeatable binds for resizing the active window
binde = , right, resizeactive, 100 0
binde = , left, resizeactive, -100 0
binde = , up, resizeactive, 0 -100
binde = , down, resizeactive, 0 100
# use reset to go back to the global submap
bind = , escape, submap, reset
# will reset the submap, meaning end the current one and return to the global one
submap = reset

# Fit size submap
# will switch to a submap called fitsize
bind = $mainMod, W, submap, fitsize
# will start a submap called "fitsize"
submap = fitsize
# sets binds for fitting columns/windows in the screen
bind = , W, scroller:fitsize, visible
bind = , W, submap, reset
bind = , right, scroller:fitsize, toend
bind = , right, submap, reset
bind = , left, scroller:fitsize, tobeg
bind = , left, submap, reset
bind = , up, scroller:fitsize, active
bind = , up, submap, reset
bind = , down, scroller:fitsize, all
bind = , down, submap, reset
# use reset to go back to the global submap
bind = , escape, submap, reset
# will reset the submap, meaning end the current one and return to the global one
submap = reset

# overview keys
# bind key to toggle overview (normal)
bind = $mainMod, tab, scroller:toggleoverview
# overview submap
# will switch to a submap called overview
bind = $mainMod, tab, submap, overview
# will start a submap called "overview"
submap = overview
bind = , right, scroller:movefocus, right
bind = , left, scroller:movefocus, left
bind = , up, scroller:movefocus, up
bind = , down, scroller:movefocus, down
# use reset to go back to the global submap
bind = , escape, scroller:toggleoverview,
bind = , escape, submap, reset
bind = , return, scroller:toggleoverview,
bind = , return, submap, reset
bind = $mainMod, tab, scroller:toggleoverview,
bind = $mainMod, tab, submap, reset
# will reset the submap, meaning end the current one and return to the global one
submap = reset

# Marks
bind = $mainMod, M, submap, marksadd
submap = marksadd
bind = , a, scroller:marksadd, a
bind = , a, submap, reset
bind = , b, scroller:marksadd, b
bind = , b, submap, reset
bind = , c, scroller:marksadd, c
bind = , c, submap, reset
bind = , escape, submap, reset
submap = reset

bind = $mainMod SHIFT, M, submap, marksdelete
submap = marksdelete
bind = , a, scroller:marksdelete, a
bind = , a, submap, reset
bind = , b, scroller:marksdelete, b
bind = , b, submap, reset
bind = , c, scroller:marksdelete, c
bind = , c, submap, reset
bind = , escape, submap, reset
submap = reset

bind = $mainMod, apostrophe, submap, marksvisit
submap = marksvisit
bind = , a, scroller:marksvisit, a
bind = , a, submap, reset
bind = , b, scroller:marksvisit, b
bind = , b, submap, reset
bind = , c, scroller:marksvisit, c
bind = , c, submap, reset
bind = , escape, submap, reset
submap = reset

bind = $mainMod CTRL, M, scroller:marksreset
```
