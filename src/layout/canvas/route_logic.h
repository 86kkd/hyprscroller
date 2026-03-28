#pragma once

#include "../../core/types.h"

namespace CanvasLayoutInternal {

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

enum class DirectionalHandoffRoute {
    NoOp,
    AdjacentLane,
    CrossMonitor,
    CreateLane,
};

bool direction_moves_between_lanes(Mode mode, Direction direction);
bool direction_inserts_before_current(Mode mode, Direction direction);
DirectionalHandoffRoute choose_directional_handoff_route(bool betweenLanes, bool hasAdjacentLane, bool hasTargetMonitor, bool allowCreate);
MoveFocusRouteAction decide_move_focus_route(bool hasLane, bool laneEmpty, bool betweenLanes, FocusMoveResult moveResult, DirectionalHandoffRoute handoffRoute);
CrossLaneMoveWindowAction decide_cross_lane_move_window_action(bool hasCurrentWindow, bool hasSourceMonitor, DirectionalHandoffRoute handoffRoute);
bool should_cross_monitor_from_empty_lane(bool laneEmpty, bool betweenLanes, bool hasTargetMonitor);
bool should_mark_special_ephemeral_lane_for_restore(bool workspaceIsSpecial, bool workspaceVisible, bool activeLaneIsEphemeral, bool activeLaneEmpty);
bool should_restore_marked_special_ephemeral_lane(bool workspaceIsSpecial, bool workspaceVisible, bool restorePending, bool activeLaneIsEphemeral, bool activeLaneEmpty);

} // namespace CanvasLayoutInternal
