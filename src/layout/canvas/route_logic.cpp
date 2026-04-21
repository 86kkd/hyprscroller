/**
 * @file route_logic.cpp
 * @brief Pure routing decisions for directional focus and move-window commands.
 *
 * The canvas layer has several tricky directional cases: stay in the same lane,
 * switch to an adjacent lane, create a temporary empty lane, or cross to
 * another monitor. This file keeps the "decision table" pure so the imperative
 * canvas code can ask for a route first and perform side effects second.
 */
#include "route_logic.h"

#include "../../core/layout_profile.h"

namespace CanvasLayoutInternal {

bool direction_moves_between_lanes(Mode mode, Direction direction) {
    return ScrollerCore::direction_moves_between_lanes(mode, direction);
}

bool direction_inserts_before_current(Mode mode, Direction direction) {
    return ScrollerCore::direction_inserts_before_current(mode, direction);
}

// Collapse the raw booleans discovered by canvas code into one high-level
// "what kind of handoff is even possible?" answer.
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

// Turn the local move result plus the handoff plan into one imperative action
// for `CanvasLayout::move_focus`. The caller performs the action later; this
// helper only decides which branch the caller should take.
MoveFocusRouteAction decide_move_focus_route(bool hasLane, bool laneEmpty, bool betweenLanes, FocusMoveResult moveResult, DirectionalHandoffRoute handoffRoute) {
    if (!hasLane)
        return MoveFocusRouteAction::DispatchBuiltin;

    if (!laneEmpty) {
        if (moveResult == FocusMoveResult::Moved)
            return MoveFocusRouteAction::FinalizeLocalMove;
        if (!betweenLanes && moveResult == FocusMoveResult::CrossMonitor)
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
        return moveResult == FocusMoveResult::CrossMonitor
            ? MoveFocusRouteAction::CrossMonitor
            : MoveFocusRouteAction::NoOp;
    }

    return MoveFocusRouteAction::NoOp;
}

// `move_window` shares the same directional-handoff shape as `move_focus`, but
// the failure policy differs: without a source window or monitor we fall back to
// Hyprland builtins instead of attempting local payload transfer.
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

// Hidden special workspaces can temporarily leave the canvas focused on a blank
// ephemeral lane. These predicates keep that hide/show restoration policy pure
// so focus code only needs to react to a yes/no answer.
bool should_mark_special_ephemeral_lane_for_restore(bool workspaceIsSpecial, bool workspaceVisible, bool activeLaneIsEphemeral, bool activeLaneEmpty) {
    return workspaceIsSpecial && !workspaceVisible && activeLaneIsEphemeral && activeLaneEmpty;
}

bool should_restore_marked_special_ephemeral_lane(bool workspaceIsSpecial, bool workspaceVisible, bool restorePending, bool activeLaneIsEphemeral, bool activeLaneEmpty) {
    return workspaceIsSpecial && workspaceVisible && restorePending && activeLaneIsEphemeral && activeLaneEmpty;
}

} // namespace CanvasLayoutInternal
