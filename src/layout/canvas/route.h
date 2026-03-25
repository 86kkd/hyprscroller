#pragma once

#include "../lane/lane.h"
#include "route_logic.h"

namespace CanvasLayoutInternal {

struct DirectionalHandoffPlan {
    DirectionalHandoffRoute route = DirectionalHandoffRoute::NoOp;
    ListNode<Lane *> *targetLaneNode = nullptr;
    PHLMONITOR targetMonitor = nullptr;
};

using MonitorResolverFn = PHLMONITOR (*)(PHLMONITOR sourceMonitor, Direction direction);

ListNode<Lane *> *adjacent_lane(ListNode<Lane *> *current, Mode mode, Direction direction);
bool should_sync_workspace_focus_before_move(ListNode<Lane *> *activeLaneNode);
PHLMONITOR resolve_monitor_in_direction(PHLMONITOR sourceMonitor, Direction direction);
DirectionalHandoffPlan plan_directional_handoff(List<Lane *> &lanes, ListNode<Lane *> *current, PHLMONITOR sourceMonitor, Mode mode, Direction direction, bool allowCreate, MonitorResolverFn monitorResolver);

} // namespace CanvasLayoutInternal
