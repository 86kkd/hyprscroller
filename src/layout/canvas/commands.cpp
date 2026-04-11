/**
 * @file commands.cpp
 * @brief Dispatcher-facing command wrappers for canvas operations.
 *
 * These methods are intentionally thin. Their job is to normalize workspace
 * focus state, resolve the active lane, and then delegate the actual layout
 * work to `Lane` or shared canvas helpers.
 */
#include <string>
#include <utility>

#include <hyprland/src/Compositor.hpp>
#include <spdlog/spdlog.h>

#include "../../core/direction.h"
#include "../../core/layout_profile.h"
#include "../lane/lane.h"
#include "layout.h"
#include "internal.h"
#include "route.h"

namespace {
// Convert a workspace to the selector string expected by Hyprland workspace
// dispatchers.
std::string workspace_selector(PHLWORKSPACE workspace) {
    if (!workspace)
        return {};

    if (!workspace->m_name.empty())
        return workspace->m_name;

    return std::to_string(workspace->m_id);
}

} // namespace

PHLMONITOR CanvasLayout::directionalMoveTargetMonitor(PHLMONITOR sourceMonitor, Direction direction) const {
    return CanvasLayoutInternal::resolve_monitor_in_direction(sourceMonitor, direction);
}

bool CanvasLayout::handoffMoveWindowAcrossMonitor(int workspace, Direction direction, Lane *sourceLane,
                                                  PHLWINDOW currentWindow, PHLMONITOR sourceMonitor,
                                                  PHLMONITOR targetMonitor) {
    if (!sourceLane || !currentWindow || !sourceMonitor || !targetMonitor)
        return false;
    auto *sourceLaneNode = getLaneNode(sourceLane);
    if (!sourceLaneNode)
        return false;

    const auto workspaceId = CanvasLayoutInternal::preferred_workspace_id(targetMonitor, workspace);
    const auto targetWorkspace = g_pCompositor->getWorkspaceByID(workspaceId);
    const auto selector = workspace_selector(targetWorkspace);
    if (!CanvasLayoutInternal::can_invoke_dispatcher("movetoworkspacesilent", selector, "move_window_cross_monitor"))
        return false;

    auto *targetLayout = CanvasLayoutInternal::get_canvas_for_workspace(workspaceId);
    if (!targetLayout)
        return false;

    auto *targetLane = static_cast<Lane *>(nullptr);
    const auto targetAnchorWindow = resolveCrossMonitorFocusTarget(
        targetLayout,
        targetMonitor,
        workspaceId,
        direction,
        currentWindow,
        &targetLane,
        nullptr);

    const auto restorePlan = sourceLane->capture_active_window_restore_plan(direction);
    auto payload = sourceLane->extract_active_window_payload();
    if (!payload)
        return true;

    const auto insertDirection = ScrollerCore::opposite_direction(direction);
    targetLayout->rememberManualCrossMonitorInsertion(currentWindow);

    if (!CanvasLayoutInternal::invoke_dispatcher("movetoworkspacesilent", selector, "move_window_cross_monitor")) {
        spdlog::warn("move_window_cross_monitor: dispatcher failed workspace={} direction={} window={}",
                     workspace,
                     ScrollerCore::direction_name(direction),
                     static_cast<const void*>(currentWindow.get()));
        targetLayout->forgetManualCrossMonitorInsertion(currentWindow);
        sourceLane->restore_active_window_payload(std::move(payload), restorePlan);
        debugVerifyLaneCache();
        return false;
    }

    targetLane = targetAnchorWindow ? targetLayout->getLaneForWindow(targetAnchorWindow) : nullptr;
    if (!targetLane)
        targetLane = targetLayout->getActiveLane();
    if (!targetLane) {
        const auto targetMode = ScrollerCore::default_mode_for_extent(targetMonitor->m_size.x, targetMonitor->m_size.y);
        targetLane = targetLayout->ensureActiveLane(targetMonitor, targetMode);
    }

    if (targetAnchorWindow && targetLane->has_window(targetAnchorWindow) && !targetLane->is_active(targetAnchorWindow))
        targetLane->focus_window(targetAnchorWindow);
    targetLane->insert_window_payload(std::move(payload), insertDirection);
    forgetWindowLane(currentWindow);
    targetLayout->rememberWindowLane(currentWindow, targetLane);
    targetLayout->forgetManualCrossMonitorInsertion(currentWindow);
    targetLayout->setActiveLane(targetLane);

    if (!dropEmptyLane(sourceLaneNode, nullptr, sourceMonitor))
        relayoutVisibleCanvas(sourceMonitor);

    targetLayout->relayoutVisibleCanvas(targetMonitor);
    targetLayout->requestWorkspaceFocusSyncSuppression();
    CanvasLayoutInternal::switch_to_window(currentWindow, true);
    debugVerifyLaneCache();
    targetLayout->debugVerifyLaneCache();
    return true;
}

void CanvasLayout::transferMoveWindowToAdjacentLane(Lane *sourceLane, PHLWINDOW currentWindow,
                                                    ListNode<Lane *> *targetLaneNode, Direction direction,
                                                    PHLMONITOR sourceMonitor) {
    if (!sourceLane || !currentWindow || !targetLaneNode)
        return;
    auto *sourceLaneNode = getLaneNode(sourceLane);
    if (!sourceLaneNode)
        return;

    auto payload = sourceLane->extract_active_window_payload();
    if (!payload)
        return;

    targetLaneNode->data()->insert_window_payload(std::move(payload), direction);
    rememberWindowLane(currentWindow, targetLaneNode->data());
    activeLane = targetLaneNode;
    finishLaneTransfer(sourceLaneNode, sourceMonitor, false, true);
    debugVerifyLaneCache();
}

void CanvasLayout::transferMoveWindowToNewLane(Lane *sourceLane, PHLWINDOW currentWindow,
                                               PHLMONITOR sourceMonitor, Mode mode, Direction direction) {
    if (!sourceLane || !currentWindow)
        return;
    auto sourceLaneNode = getLaneNode(sourceLane);
    if (!sourceLaneNode)
        return;

    auto payload = sourceLane->extract_active_window_payload();
    if (!payload)
        return;

    auto *newLane = new Lane(sourceMonitor, mode);
    auto newLaneNode = insertLaneNode(newLane, direction, sourceLaneNode);

    newLane->insert_window_payload(std::move(payload), direction);
    rememberWindowLane(currentWindow, newLane);
    activeLane = newLaneNode;
    finishLaneTransfer(sourceLaneNode, sourceMonitor, false, true);
    debugVerifyLaneCache();
}

void CanvasLayout::handleMoveWindowWithinLane(int workspace, Direction direction, Lane *lane,
                                              PHLWINDOW currentWindow, PHLMONITOR sourceMonitor) {
    if (!lane)
        return;

    const auto mode = lane->get_mode();
    const auto backward = ScrollerCore::local_item_backward_direction(mode);
    const auto forward = ScrollerCore::local_item_forward_direction(mode);
    const bool movesBetweenStacks = direction == backward || direction == forward;
    if (!movesBetweenStacks) {
        lane->move_active_stack(direction);
        CanvasLayoutInternal::switch_to_window(lane->get_active_window());
        return;
    }

    const auto targetMonitor = directionalMoveTargetMonitor(sourceMonitor, direction);
    const bool sourceStackHasMultipleWindows = lane->active_stack_has_multiple_windows();
    if (sourceStackHasMultipleWindows) {
        if (lane->active_item_at_edge(direction) && targetMonitor) {
            if (!handoffMoveWindowAcrossMonitor(workspace, direction, lane, currentWindow, sourceMonitor, targetMonitor))
                CanvasLayoutInternal::dispatch_directional_builtin("movewindow", direction);
            return;
        }

        lane->move_active_window_to_new_stack(direction);
        CanvasLayoutInternal::switch_to_window(lane->get_active_window());
        return;
    }

    if (!lane->active_item_at_edge(direction)) {
        lane->move_active_window_to_adjacent_stack(direction);
        CanvasLayoutInternal::switch_to_window(lane->get_active_window());
        return;
    }

    if (targetMonitor) {
        if (!handoffMoveWindowAcrossMonitor(workspace, direction, lane, currentWindow, sourceMonitor, targetMonitor))
            CanvasLayoutInternal::dispatch_directional_builtin("movewindow", direction);
        return;
    }
}

void CanvasLayout::handleMoveWindowAcrossLanes(int workspace, Direction direction, Lane *lane,
                                               PHLWINDOW currentWindow, PHLMONITOR sourceMonitor,
                                               Mode mode) {
    if (!lane)
        return;

    const auto handoffPlan = currentWindow && sourceMonitor
        ? CanvasLayoutInternal::plan_directional_handoff(
              lanes, activeLane, sourceMonitor, mode, direction, true, CanvasLayoutInternal::resolve_monitor_in_direction)
        : CanvasLayoutInternal::DirectionalHandoffPlan{};

    switch (CanvasLayoutInternal::decide_cross_lane_move_window_action(currentWindow != nullptr, sourceMonitor != nullptr, handoffPlan.route)) {
    case CanvasLayoutInternal::CrossLaneMoveWindowAction::BuiltinFallback:
        CanvasLayoutInternal::dispatch_directional_builtin("movewindow", direction);
        return;
    case CanvasLayoutInternal::CrossLaneMoveWindowAction::AdjacentLaneTransfer:
        transferMoveWindowToAdjacentLane(lane, currentWindow, handoffPlan.targetLaneNode, direction, sourceMonitor);
        return;
    case CanvasLayoutInternal::CrossLaneMoveWindowAction::CrossMonitorTransfer:
        if (!handoffMoveWindowAcrossMonitor(workspace, direction, lane, currentWindow, sourceMonitor, handoffPlan.targetMonitor))
            CanvasLayoutInternal::dispatch_directional_builtin("movewindow", direction);
        return;
    case CanvasLayoutInternal::CrossLaneMoveWindowAction::CreateLaneTransfer:
        transferMoveWindowToNewLane(lane, currentWindow, sourceMonitor, mode, direction);
        return;
    case CanvasLayoutInternal::CrossLaneMoveWindowAction::NoOp:
        return;
    }
}

// Cycle the active stack width or active window height by one preset step.
void CanvasLayout::cycle_window_size(int workspace, int step)
{
    (void)workspace;
    withActiveLane(ActiveLaneSyncPolicy::WorkspaceFocus, [step](Lane *lane) {
        lane->resize_active_stack(step);
    });
}

// Move the focused window or stack according to lane/mode routing rules.
void CanvasLayout::move_window(int workspace, Direction direction) {
    withActiveLane(ActiveLaneSyncPolicy::WorkspaceFocus, [&](Lane *lane) {
        const auto mode = lane->get_mode();
        const auto currentWindow = lane->get_active_window();
        const auto sourceMonitor = currentWindow ? g_pCompositor->getMonitorFromID(currentWindow->monitorID()) : getVisibleCanvasMonitor();

        if (CanvasLayoutInternal::direction_moves_between_lanes(mode, direction)) {
            handleMoveWindowAcrossLanes(workspace, direction, lane, currentWindow, sourceMonitor, mode);
            return;
        }

        handleMoveWindowWithinLane(workspace, direction, lane, currentWindow, sourceMonitor);
    });
}

// Align the active stack/window inside the current lane viewport.
void CanvasLayout::align_window(int workspace, Direction direction) {
    (void)workspace;
    withActiveLane(ActiveLaneSyncPolicy::WorkspaceFocus, [direction](Lane *lane) {
        lane->align_stack(direction);
    });
}

// Move the active window into the previous stack.
void CanvasLayout::admit_window_left(int workspace) {
    (void)workspace;
    withActiveLane(ActiveLaneSyncPolicy::WorkspaceFocus, [](Lane *lane) {
        lane->admit_window_left();
    });
}

// Split the active window into a new stack to the right.
void CanvasLayout::expel_window_right(int workspace) {
    (void)workspace;
    withActiveLane(ActiveLaneSyncPolicy::WorkspaceFocus, [](Lane *lane) {
        lane->expel_window_right();
    });
}

// Change the active lane traversal mode.
void CanvasLayout::set_mode(int workspace, Mode mode) {
    (void)workspace;
    withActiveLane(ActiveLaneSyncPolicy::WorkspaceFocus, [mode](Lane *lane) {
        lane->set_mode(mode);
    });
}

// Resize the requested visible range so it fills the current lane viewport.
void CanvasLayout::fit_size(int workspace, FitSize fitsize) {
    (void)workspace;
    withActiveLane(ActiveLaneSyncPolicy::WorkspaceFocus, [fitsize](Lane *lane) {
        lane->fit_size(fitsize);
    });
}

// Toggle lane overview projection.
void CanvasLayout::toggle_overview(int workspace) {
    (void)workspace;
    withActiveLane(ActiveLaneSyncPolicy::WorkspaceFocus, [](Lane *lane) {
        lane->toggle_overview();
    });
}

// Toggle scroller-managed fullscreen/expanded behavior.
void CanvasLayout::toggle_fullscreen(int workspace) {
    (void)workspace;
    const auto syncPolicy = consumeActiveLaneSyncPolicy();
    withActiveLane(syncPolicy, [](Lane *lane) {
        lane->toggle_fullscreen_active_window();
    });
}

// Move the active stack into a new persistent neighboring lane.
void CanvasLayout::create_lane(int workspace, Direction direction) {
    (void)workspace;
    withActiveLane(ActiveLaneSyncPolicy::WorkspaceFocus, [&](Lane *lane) {
        if (!activeLane)
            return;

        auto stack = lane->extract_active_stack();
        if (!stack)
            return;

        auto currentLaneNode = activeLane;
        auto newLane = new Lane(stack);
        auto newLaneNode = insertLaneNode(newLane, direction, currentLaneNode);

        rememberLaneWindows(newLane);
        activeLane = newLaneNode;
        finishLaneTransfer(currentLaneNode, nullptr, false, true);
        debugVerifyLaneCache();
    });
}

// Change the active lane without moving any window data.
void CanvasLayout::focus_lane(int workspace, Direction direction) {
    (void)workspace;
    if (!activeLane || lanes.size() < 2)
        return;

    auto target = activeLane;
    switch (direction) {
    case Direction::Left:
    case Direction::Up:
        target = activeLane->prev() ? activeLane->prev() : lanes.last();
        break;
    case Direction::Right:
    case Direction::Down:
        target = activeLane->next() ? activeLane->next() : lanes.first();
        break;
    case Direction::Begin:
        target = lanes.first();
        break;
    case Direction::End:
        target = lanes.last();
        break;
    default:
        return;
    }

    if (!target || target == activeLane)
        return;

    activeLane = target;
    if (const auto window = activeLane->data()->get_active_window())
        CanvasLayoutInternal::switch_to_window(window, true);
}
