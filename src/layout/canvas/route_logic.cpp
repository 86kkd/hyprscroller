#include "route_logic.h"

#include "../../core/layout_profile.h"

namespace CanvasLayoutInternal {

bool direction_moves_between_lanes(Mode mode, Direction direction) {
    return ScrollerCore::direction_moves_between_lanes(mode, direction);
}

bool direction_inserts_before_current(Mode mode, Direction direction) {
    return ScrollerCore::direction_inserts_before_current(mode, direction);
}

DirectionalHandoffRoute choose_directional_handoff_route(bool betweenLanes, bool hasAdjacentLane, bool hasTargetMonitor, bool allowCreate) {
    if (!betweenLanes)
        return DirectionalHandoffRoute::NoOp;
    if (hasAdjacentLane)
        return DirectionalHandoffRoute::AdjacentLane;
    if (hasTargetMonitor)
        return DirectionalHandoffRoute::CrossMonitor;
    if (allowCreate)
        return DirectionalHandoffRoute::CreateLane;
    return DirectionalHandoffRoute::NoOp;
}

MoveFocusRouteAction decide_move_focus_route(bool hasLane, bool laneEmpty, bool betweenLanes, FocusMoveResult moveResult, DirectionalHandoffRoute handoffRoute) {
    if (!hasLane)
        return MoveFocusRouteAction::DispatchBuiltin;

    if (!laneEmpty) {
        if (moveResult == FocusMoveResult::Moved)
            return MoveFocusRouteAction::FinalizeLocalMove;
        if (moveResult == FocusMoveResult::CrossMonitor)
            return MoveFocusRouteAction::CrossMonitor;
    }

    if (!betweenLanes)
        return MoveFocusRouteAction::NoOp;

    switch (handoffRoute) {
    case DirectionalHandoffRoute::AdjacentLane:
        return MoveFocusRouteAction::AdjacentLane;
    case DirectionalHandoffRoute::CrossMonitor:
        return MoveFocusRouteAction::CrossMonitor;
    case DirectionalHandoffRoute::CreateLane:
        return MoveFocusRouteAction::CreateLane;
    case DirectionalHandoffRoute::NoOp:
        return MoveFocusRouteAction::NoOp;
    }

    return MoveFocusRouteAction::NoOp;
}

CrossLaneMoveWindowAction decide_cross_lane_move_window_action(bool hasCurrentWindow, bool hasSourceMonitor, DirectionalHandoffRoute handoffRoute) {
    if (!hasCurrentWindow || !hasSourceMonitor)
        return CrossLaneMoveWindowAction::BuiltinFallback;

    switch (handoffRoute) {
    case DirectionalHandoffRoute::AdjacentLane:
        return CrossLaneMoveWindowAction::AdjacentLaneTransfer;
    case DirectionalHandoffRoute::CrossMonitor:
        return CrossLaneMoveWindowAction::CrossMonitorTransfer;
    case DirectionalHandoffRoute::CreateLane:
        return CrossLaneMoveWindowAction::CreateLaneTransfer;
    case DirectionalHandoffRoute::NoOp:
        return CrossLaneMoveWindowAction::NoOp;
    }

    return CrossLaneMoveWindowAction::NoOp;
}

bool should_cross_monitor_from_empty_lane(bool laneEmpty, bool betweenLanes, bool hasTargetMonitor) {
    return laneEmpty && !betweenLanes && hasTargetMonitor;
}

bool should_mark_special_ephemeral_lane_for_restore(bool workspaceIsSpecial, bool workspaceVisible, bool activeLaneIsEphemeral, bool activeLaneEmpty) {
    return workspaceIsSpecial && !workspaceVisible && activeLaneIsEphemeral && activeLaneEmpty;
}

bool should_restore_marked_special_ephemeral_lane(bool workspaceIsSpecial, bool workspaceVisible, bool restorePending, bool activeLaneIsEphemeral, bool activeLaneEmpty) {
    return workspaceIsSpecial && workspaceVisible && restorePending && activeLaneIsEphemeral && activeLaneEmpty;
}

} // namespace CanvasLayoutInternal
