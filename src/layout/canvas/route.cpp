#include "route.h"

#include <hyprland/src/Compositor.hpp>

#include "internal.h"

namespace {
ListNode<Lane *> *edge_lane_anchor(List<Lane *> &lanes, Mode mode, Direction direction) {
    if (lanes.empty())
        return nullptr;

    if (CanvasLayoutInternal::direction_inserts_before_current(mode, direction))
        return lanes.first();

    return lanes.last();
}
} // namespace

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

ListNode<Lane *> *adjacent_lane(ListNode<Lane *> *current, Mode mode, Direction direction) {
    if (!current)
        return nullptr;

    switch (mode) {
    case Mode::Row:
        if (direction == Direction::Up)
            return current->prev();
        if (direction == Direction::Down)
            return current->next();
        break;
    case Mode::Column:
        if (direction == Direction::Left)
            return current->prev();
        if (direction == Direction::Right)
            return current->next();
        break;
    }

    return nullptr;
}

bool should_sync_workspace_focus_before_move(ListNode<Lane *> *activeLaneNode) {
    if (!activeLaneNode || !activeLaneNode->data())
        return true;

    const auto lane = activeLaneNode->data();
    return !(lane->is_ephemeral() && lane->empty());
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

PHLMONITOR resolve_monitor_in_direction(PHLMONITOR sourceMonitor, Direction direction) {
    const auto monitorDirection = direction_to_math(direction);
    if (!g_pCompositor || !sourceMonitor || !monitorDirection)
        return nullptr;

    return g_pCompositor->getMonitorInDirection(sourceMonitor, *monitorDirection);
}

DirectionalHandoffPlan plan_directional_handoff(List<Lane *> &lanes, ListNode<Lane *> *current, PHLMONITOR sourceMonitor,
                                                Mode mode, Direction direction, bool allowCreate, MonitorResolverFn monitorResolver) {
    const auto betweenLanes = direction_moves_between_lanes(mode, direction);
    auto *targetLaneNode = adjacent_lane(current, mode, direction);
    const auto targetMonitor = monitorResolver ? monitorResolver(sourceMonitor, direction) : nullptr;
    const auto route = choose_directional_handoff_route(betweenLanes, targetLaneNode != nullptr, targetMonitor != nullptr, allowCreate);

    switch (route) {
    case DirectionalHandoffRoute::AdjacentLane:
        return {
            .route = route,
            .targetLaneNode = targetLaneNode,
        };
    case DirectionalHandoffRoute::CrossMonitor:
        return {
            .route = route,
            .targetMonitor = targetMonitor,
        };
    case DirectionalHandoffRoute::CreateLane:
        return {
            .route = route,
            .targetLaneNode = edge_lane_anchor(lanes, mode, direction),
        };
    case DirectionalHandoffRoute::NoOp:
        return {};
    }

    return {};
}

} // namespace CanvasLayoutInternal
