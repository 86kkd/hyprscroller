# Architecture Guide

This document is the newcomer-oriented map of the codebase. It is meant to
answer one question first:

How does a Hyprland event or dispatcher end up moving windows inside
hyprscroller?

## Runtime At A Glance

Most of the project can be understood as one pipeline:

1. Hyprland loads the plugin through `PLUGIN_INIT` in
   [`src/main.cpp`](../src/main.cpp).
2. `PLUGIN_INIT` registers:
   - config values
   - custom dispatchers such as `scroller:movefocus`
   - `CanvasLayout` as the tiled layout algorithm named `scroller`
3. Hyprland later calls either:
   - dispatcher functions in [`src/dispatchers.cpp`](../src/dispatchers.cpp)
     when the user presses a keybinding
   - tiled-algorithm callbacks on `CanvasLayout` when windows are created,
     removed, moved, or relaid out
4. `CanvasLayout` routes that request to its current `Lane`.
5. Each `Lane` routes the operation to one or more `Stack` objects.
6. `Stack` updates window membership, active selection, and geometry.

If you only want the shortest reading path through the project, follow files in
this order:

1. [`src/main.cpp`](../src/main.cpp)
2. [`src/dispatchers.cpp`](../src/dispatchers.cpp)
3. [`src/layout/canvas/layout.h`](../src/layout/canvas/layout.h)
4. [`src/layout/canvas/core.cpp`](../src/layout/canvas/core.cpp)
5. [`src/layout/canvas/focus.cpp`](../src/layout/canvas/focus.cpp)
6. [`src/layout/canvas/commands.cpp`](../src/layout/canvas/commands.cpp)
7. [`src/layout/lane/core.cpp`](../src/layout/lane/core.cpp),
   [`src/layout/lane/actions.cpp`](../src/layout/lane/actions.cpp), and
   [`src/layout/lane/geometry.cpp`](../src/layout/lane/geometry.cpp)
8. [`src/model/stack_core.cpp`](../src/model/stack_core.cpp),
   [`src/model/stack_membership.cpp`](../src/model/stack_membership.cpp), and
   [`src/model/stack_geometry.cpp`](../src/model/stack_geometry.cpp)

## Core Concepts

There are four main runtime objects.

### Plugin

The plugin is the Hyprland-facing shell around the project. It lives mostly in
[`src/main.cpp`](../src/main.cpp).

Its job is to:

- register config values
- register custom dispatchers
- register `CanvasLayout` as the `scroller` tiled algorithm
- initialize logging and overview renderer hooks

It does not contain layout policy itself.

### CanvasLayout

`CanvasLayout` is the top-level controller for one workspace/canvas instance.
Its public definition is in
[`src/layout/canvas/layout.h`](../src/layout/canvas/layout.h).

You can think of it as the bridge between Hyprland and the plugin's own layout
model.

`CanvasLayout` is responsible for:

- owning the ordered list of `Lane` objects in one workspace
- translating Hyprland callbacks into model operations
- exposing dispatcher-facing commands such as `move_focus` and `move_window`
- coordinating cross-lane and cross-monitor handoff
- keeping caches and active-lane state synchronized with Hyprland focus

### Lane

A `Lane` is the next level down. It groups one ordered strip of stacks inside a
canvas.

In row mode, a lane behaves like the horizontal sequence you scroll across.
In column mode, a lane still owns stacks, but the active stack semantics and
movement rules change so vertical stacking becomes the dominant local action.

Lanes are the place where the layout starts to feel "PaperWM-like": they decide
which stack is active, which adjacent stack should receive a moved window, and
how stack geometry fits inside the visible canvas.

### Stack

A `Stack` owns an ordered list of windows and the state attached to that local
group.

It is responsible for:

- which window is active inside the stack
- adding, removing, and restoring window payloads
- size/alignment policy for that local group
- geometry calculations for the windows it owns

If a `Lane` answers "which stack should handle this operation?", `Stack`
answers "what exactly happens to the windows inside that stack?"

### Window

The actual Hyprland window object still comes from Hyprland. Hyprscroller keeps
lightweight bookkeeping around it, but the compositor remains the source of
truth for the real client.

## Main Flows

### Startup

Startup begins in [`src/main.cpp`](../src/main.cpp).

Read `PLUGIN_INIT` first. It does three important things in order:

1. sets up logging
2. registers config values and dispatchers
3. registers `CanvasLayout` as the tiled algorithm for `layout = scroller`

After that, Hyprland is free to call the plugin through dispatcher invocations
or tiled-layout callbacks.

### New Tiled Window

This is the simplest lifecycle to trace:

1. Hyprland creates a tiled target.
2. `CanvasLayout::newTarget` or `CanvasLayout::movedTarget` is called.
3. Those functions forward to `CanvasLayout::onWindowCreatedTiling`.
4. `onWindowCreatedTiling` finds or creates the active lane.
5. The lane inserts the new window into its active stack according to the
   current mode.
6. Ownership caches are updated so future lookups can jump directly from window
   to lane.

The main implementation lives in
[`src/layout/canvas/core.cpp`](../src/layout/canvas/core.cpp).

### Focus Movement

Directional focus starts in a dispatcher:

1. Hyprland runs `scroller:movefocus`.
2. [`src/dispatchers.cpp`](../src/dispatchers.cpp) parses the direction and
   resolves the active `CanvasLayout`.
3. `CanvasLayout::move_focus` decides whether the request is:
   - a local move inside the current lane
   - a move to an adjacent lane
   - a move that should create a temporary empty lane
   - a cross-monitor handoff
4. The chosen lane or target monitor is activated and the compositor focus is
   updated.

The main implementation lives in
[`src/layout/canvas/focus.cpp`](../src/layout/canvas/focus.cpp).

### Window Movement

Directional window movement starts similarly:

1. Hyprland runs `scroller:movewindow`.
2. The dispatcher resolves the active `CanvasLayout`.
3. `CanvasLayout::move_window` chooses whether the window stays:
   - in the same stack
   - in the same lane but a different stack
   - in an adjacent lane
   - on another monitor
4. Payload extraction and insertion helpers move the active window while
   preserving the source stack or lane state when possible.

The main implementation lives in
[`src/layout/canvas/commands.cpp`](../src/layout/canvas/commands.cpp) and
[`src/layout/lane/actions.cpp`](../src/layout/lane/actions.cpp).

### Overview

Overview is a separate read-only navigation layer.

Its logic is split into:

- overview session/model code under [`src/overview`](../src/overview)
- snapshot building inside `CanvasLayout`
- overview-specific tests in
  [`tests/overview_logic_tests.cpp`](../tests/overview_logic_tests.cpp)

The important thing to remember is that overview does not mutate the real lane
or stack geometry while it is open. It navigates a logical snapshot.

## How The Source Tree Is Split

### `src/main.cpp`

Plugin ABI entrypoints and one-time registration.

### `src/dispatchers.cpp`

String argument parsing and routing from Hyprland commands into strongly typed
`CanvasLayout` methods.

### `src/layout/canvas`

Workspace-level control plane.

- `core.cpp`: target callbacks, workspace ownership, lane cache, lifecycle
- `focus.cpp`: focus routing and cross-monitor focus handoff
- `commands.cpp`: dispatcher-facing mutating commands such as movewindow
- `dispatch_logic.cpp` and `route_logic.cpp`: pure logic helpers that keep
  decision-heavy code unit-testable

### `src/layout/lane`

Lane-level behavior.

- `core.cpp`: lane ownership and active-stack bookkeeping
- `actions.cpp`: stack/window movement and transfer actions
- `geometry.cpp`: lane-local geometry work

### `src/model`

Local data structures.

- `stack_core.cpp`: stack construction and control flow helpers
- `stack_membership.cpp`: add/remove/restore/extract window membership logic
- `stack_geometry.cpp`: sizing, fitting, and placement behavior

## Tests

The test target does not boot Hyprland. It focuses on the pure logic that is
worth keeping stable across refactors.

- [`tests/core_logic_tests.cpp`](../tests/core_logic_tests.cpp)
  covers shared helpers and low-level logic.
- [`tests/layout_logic_tests.cpp`](../tests/layout_logic_tests.cpp)
  covers layout routing and handoff decision helpers.
- [`tests/overview_logic_tests.cpp`](../tests/overview_logic_tests.cpp)
  covers the overview model and selection logic.

When you change real stateful behavior in `CanvasLayout`, `Lane`, or `Stack`,
the automated tests are necessary but not sufficient. Pair them with the manual
[smoke test checklist](./smoke-test-checklist.md) and, when needed, the
[Hyprland testing notes](./hyprland-testing.md). For changes that need nested
repro scripts or staged debug work, also follow the
[feature/debug workflow](./feature-debug-workflow.md).

## Suggested Reading Strategy

If you are new to the project, use this order:

1. Read this file once without opening code.
2. Open [`src/main.cpp`](../src/main.cpp) and find `PLUGIN_INIT`.
3. Open [`src/dispatchers.cpp`](../src/dispatchers.cpp) and trace one command,
   such as `dispatch_movefocus`.
4. Open [`src/layout/canvas/layout.h`](../src/layout/canvas/layout.h) and read
   the public methods on `CanvasLayout`.
5. Trace one lifecycle end to end:
   - new window: `newTarget -> onWindowCreatedTiling`
   - focus: `dispatch_movefocus -> move_focus`
   - move window: `dispatch_movewindow -> move_window`
6. Only then dive into lane and stack code.

That order keeps the mental model top-down. If you start at `Stack` first, the
local code will make sense, but the overall runtime picture usually will not.
