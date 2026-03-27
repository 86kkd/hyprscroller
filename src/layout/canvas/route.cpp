#include "route.h"

#include <hyprland/src/Compositor.hpp>

#include "../../core/layout_profile.h"
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

ListNode<Lane *> *adjacent_lane(ListNode<Lane *> *current, Mode mode, Direction direction) {
    if (!current)
        return nullptr;

    if (direction == ScrollerCore::lane_backward_direction(mode))
        return current->prev();
    if (direction == ScrollerCore::lane_forward_direction(mode))
        return current->next();
    return nullptr;
}

bool should_sync_workspace_focus_before_move(ListNode<Lane *> *activeLaneNode) {
    if (!activeLaneNode || !activeLaneNode->data())
        return true;

    const auto lane = activeLaneNode->data();
    return !(lane->is_ephemeral() && lane->empty());
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
