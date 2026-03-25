# Hyprscroller Smoke Test Checklist

Use this checklist after changing `src/layout/canvas/*`, `src/layout/lane/*`, or `src/model/stack.*`.

## Preconditions

- Build the plugin with `make debug`.
- Start Hyprland with the plugin enabled.
- Prefer two monitors for the full pass: one landscape and one portrait if available.
- Prepare one normal workspace and one special workspace.

## Checklist

1. Open three tiled windows in `row` mode on one workspace and confirm the newest window appears next to the latest focused window, not at the far end.
2. Run `scroller:movefocus l/r` across the row and confirm focus changes stay inside the lane until the edge.
3. Run `scroller:movewindow l/r` inside the same row and confirm window order changes without losing geometry or leaving blank space.
4. Switch to `column` mode, add at least three windows to one stack, and confirm `scroller:movefocus u/d` moves inside the stack correctly.
5. In `column` mode, run `scroller:admitwindow` and `scroller:expelwindow` and confirm the moved window keeps focus and no window disappears.
6. In `row` mode, run `scroller:cyclesize` and `scroller:fitsize visible` and confirm all visible columns remain on-screen.
7. Toggle `scroller:togglefullscreen` in `row` mode, then move focus left/right and confirm the layout restores cleanly after toggling back.
8. Toggle `scroller:togglefullscreen` in `column` mode and confirm expanded-window behavior still allows focus movement and relayout after exit.
9. Create or enter an empty ephemeral lane via directional focus and confirm it is cleaned up once focus returns to a non-empty lane.
10. Move a window to an adjacent lane and confirm the source lane is removed only when empty.
11. Move a window from the landscape monitor to the portrait monitor and back with `scroller:movewindow`, confirming target insertion happens beside the latest focused window on the destination monitor.
12. Move focus across monitors with `scroller:movefocus` and confirm focus lands on the target monitor without bouncing back on the next command.
13. Repeat cross-monitor `movewindow` while the target or crossed neighbor is scroller-fullscreen and confirm there is no half-monitor blank region.
14. Open a special workspace containing scroller windows, toggle it visible/hidden, and confirm relayout still uses the correct monitor.
15. On an empty or newly created workspace, run `scroller:movefocus` or `scroller:movewindow` once and confirm builtin fallback behavior does not crash or wedge focus.

## Notes

- If any step fails, capture the exact dispatcher, workspace, monitor orientation, and whether the active lane was empty/fullscreen/special.
- Re-run steps 10-15 after any change touching cross-monitor handoff, dispatcher helpers, or focus suppression.
