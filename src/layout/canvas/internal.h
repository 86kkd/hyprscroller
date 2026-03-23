#pragma once

#include <optional>

#include "../lane/lane.h"
#include "layout.h"

namespace CanvasLayoutInternal {
struct CanvasBounds {
    ScrollerCore::Box full;
    ScrollerCore::Box max;
    int               gap;
};

enum class DirectionalHandoffRoute {
    NoOp,
    AdjacentLane,
    CrossMonitor,
    CreateLane,
};

struct DirectionalHandoffPlan {
    DirectionalHandoffRoute route = DirectionalHandoffRoute::NoOp;
    ListNode<Lane*>*        targetLaneNode = nullptr;
    PHLMONITOR              targetMonitor = nullptr;
};

const char*                     direction_name(Direction direction);
const char*                     direction_dispatch_arg(Direction direction);
bool                            direction_moves_between_lanes(Mode mode, Direction direction);
bool                            direction_inserts_before_current(Mode mode, Direction direction);
ListNode<Lane*>*                adjacent_lane(ListNode<Lane*>* current, Mode mode, Direction direction);
DirectionalHandoffPlan          plan_directional_handoff(List<Lane*>& lanes, ListNode<Lane*>* current, PHLMONITOR sourceMonitor, Mode mode, Direction direction, bool allow_create);
CanvasBounds                    compute_canvas_bounds(PHLMONITOR monitor);
void                            recalculate_workspace_lane(Lane* lane, PHLMONITOR monitor, PHLWORKSPACE workspace, bool honor_fullscreen);
WORKSPACEID                     preferred_workspace_id(PHLMONITOR monitor, WORKSPACEID source_workspace_id);
PHLMONITOR                      visible_monitor_for_workspace(PHLWORKSPACE workspace);
CanvasLayout*                 get_canvas_for_workspace(WORKSPACEID workspace_id);
std::optional<Math::eDirection> direction_to_math(Direction direction);
PHLWINDOW                       pick_cross_monitor_target_window(PHLMONITOR monitor, WORKSPACEID workspace_id, Direction direction, PHLWINDOW source_window);
void                            dispatch_directional_builtin(const char* dispatcher, Direction direction);
void                            dispatch_builtin_movefocus(Direction direction);
void                            focus_window_monitor(PHLWINDOW window);
void                            switch_to_window(PHLWINDOW window, bool warp_cursor = false);
int                             get_workspace_id();
}
