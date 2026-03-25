#pragma once

#include "../lane/lane.h"

namespace CanvasLayoutInternal {

enum class DirectionalHandoffRoute {
    NoOp,
    AdjacentLane,
    CrossMonitor,
    CreateLane,
};

struct DirectionalHandoffPlan {
    DirectionalHandoffRoute route = DirectionalHandoffRoute::NoOp;
    ListNode<Lane *> *targetLaneNode = nullptr;
    PHLMONITOR targetMonitor = nullptr;
};

enum class MoveFocusRouteAction {
    DispatchBuiltin,
    NoOp,
    AdjacentLane,
    CrossMonitor,
    CreateLane,
    FinalizeLocalMove,
};

enum class CrossLaneMoveWindowAction {
    BuiltinFallback,
    NoOp,
    AdjacentLaneTransfer,
    CrossMonitorTransfer,
    CreateLaneTransfer,
};

using MonitorResolverFn = PHLMONITOR (*)(PHLMONITOR sourceMonitor, Direction direction);

bool direction_moves_between_lanes(Mode mode, Direction direction);
bool direction_inserts_before_current(Mode mode, Direction direction);
ListNode<Lane *> *adjacent_lane(ListNode<Lane *> *current, Mode mode, Direction direction);
bool should_sync_workspace_focus_before_move(ListNode<Lane *> *activeLaneNode);
DirectionalHandoffRoute choose_directional_handoff_route(bool betweenLanes, bool hasAdjacentLane, bool hasTargetMonitor, bool allowCreate);
MoveFocusRouteAction decide_move_focus_route(bool hasLane, bool laneEmpty, bool betweenLanes, FocusMoveResult moveResult, DirectionalHandoffRoute handoffRoute);
CrossLaneMoveWindowAction decide_cross_lane_move_window_action(bool hasCurrentWindow, bool hasSourceMonitor, DirectionalHandoffRoute handoffRoute);
PHLMONITOR resolve_monitor_in_direction(PHLMONITOR sourceMonitor, Direction direction);
DirectionalHandoffPlan plan_directional_handoff(List<Lane *> &lanes, ListNode<Lane *> *current, PHLMONITOR sourceMonitor, Mode mode, Direction direction, bool allowCreate, MonitorResolverFn monitorResolver);

} // namespace CanvasLayoutInternal
