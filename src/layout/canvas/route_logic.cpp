#include "route_logic.h"

namespace CanvasLayoutInternal {

bool direction_moves_between_lanes(Mode mode, Direction direction) {
    switch (mode) {
    case Mode::Row:
        return direction == Direction::Up || direction == Direction::Down;
    case Mode::Column:
        return direction == Direction::Left || direction == Direction::Right;
    }

    return false;
}

bool direction_inserts_before_current(Mode mode, Direction direction) {
    switch (mode) {
    case Mode::Row:
        return direction == Direction::Up || direction == Direction::Begin;
    case Mode::Column:
        return direction == Direction::Left || direction == Direction::Begin;
    }

    return false;
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

} // namespace CanvasLayoutInternal
