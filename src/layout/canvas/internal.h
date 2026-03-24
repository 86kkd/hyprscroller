#pragma once

#include <optional>
#include <string_view>

#include "../lane/lane.h"
#include "layout.h"

namespace CanvasLayoutInternal {
/**
 * @brief Cached monitor bounds used by canvas and lane relayout code.
 *
 * `full` keeps the raw monitor rectangle, while `max` keeps the workarea after
 * Hyprland reserved areas and outer gaps are applied.
 */
struct CanvasBounds {
    ScrollerCore::Box full;
    ScrollerCore::Box max;
    int               gap;
};

/**
 * @brief Planned routing result for directional focus/window handoff.
 */
enum class DirectionalHandoffRoute {
    NoOp,
    AdjacentLane,
    CrossMonitor,
    CreateLane,
};

/**
 * @brief Routing plan shared by movefocus and movewindow.
 *
 * Depending on the direction and current canvas state, a command can stay on
 * the current lane, jump to an adjacent lane, cross to another monitor, or
 * create a new lane.
 */
struct DirectionalHandoffPlan {
    DirectionalHandoffRoute route = DirectionalHandoffRoute::NoOp;
    ListNode<Lane*>*        targetLaneNode = nullptr;
    PHLMONITOR              targetMonitor = nullptr;
};

// Return true when a direction should move between lanes for the current mode.
bool                            direction_moves_between_lanes(Mode mode, Direction direction);
// Return true when inserting or creating in this direction should happen before the current lane.
bool                            direction_inserts_before_current(Mode mode, Direction direction);
// Return the neighboring lane in the given logical direction.
ListNode<Lane*>*                adjacent_lane(ListNode<Lane*>* current, Mode mode, Direction direction);
// Compute the shared lane/monitor/create routing plan for directional actions.
DirectionalHandoffPlan          plan_directional_handoff(List<Lane*>& lanes, ListNode<Lane*>* current, PHLMONITOR sourceMonitor, Mode mode, Direction direction, bool allow_create);
// Compute monitor bounds once so canvas and lane logic use the same workarea math.
CanvasBounds                    compute_canvas_bounds(PHLMONITOR monitor);
// Recalculate one lane against the given workspace/monitor pair.
void                            recalculate_workspace_lane(Lane* lane, PHLMONITOR monitor, PHLWORKSPACE workspace, bool honor_fullscreen);
// Pick the preferred visible workspace on a monitor for cross-monitor handoff.
WORKSPACEID                     preferred_workspace_id(PHLMONITOR monitor, WORKSPACEID source_workspace_id);
// Resolve the monitor currently showing a workspace, including special workspaces.
PHLMONITOR                      visible_monitor_for_workspace(PHLWORKSPACE workspace);
// Lookup the canvas layout instance that owns a workspace.
CanvasLayout*                   get_canvas_for_workspace(WORKSPACEID workspace_id);
// Translate plugin direction to Hyprland monitor direction when possible.
std::optional<Math::eDirection> direction_to_math(Direction direction);
// Pick the best target window on another monitor when crossing focus.
PHLWINDOW                       pick_cross_monitor_target_window(PHLMONITOR monitor, WORKSPACEID workspace_id, Direction direction, PHLWINDOW source_window);
// Return true when a dispatcher call is well-formed and the target dispatcher exists.
bool                            can_invoke_dispatcher(const char* dispatcher, std::string_view arg, const char* context = nullptr);
// Call a Hyprland dispatcher through the shared checked invocation path.
bool                            invoke_dispatcher(const char* dispatcher, std::string_view arg, const char* context = nullptr);
// Dispatch a Hyprland builtin directional command with shared direction mapping.
void                            dispatch_directional_builtin(const char* dispatcher, Direction direction);
// Thin wrapper for builtin movefocus dispatch.
void                            dispatch_builtin_movefocus(Direction direction);
// Focus the monitor hosting a target window before focusing the window itself.
void                            focus_window_monitor(PHLWINDOW window);
// Focus a target window and optionally warp the cursor to it.
void                            switch_to_window(PHLWINDOW window, bool warp_cursor = false);
// Return the workspace id associated with the current canvas command context.
int                             get_workspace_id();
}
