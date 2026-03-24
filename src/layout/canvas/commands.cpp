/**
 * @file commands.cpp
 * @brief Dispatcher-facing command wrappers for canvas operations.
 *
 * These methods are intentionally thin. Their job is to normalize workspace
 * focus state, resolve the active lane, and then delegate the actual layout
 * work to `Lane` or shared canvas helpers.
 */
#include <string>

#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/managers/KeybindManager.hpp>

#include "../lane/lane.h"
#include "layout.h"
#include "internal.h"

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

Direction opposite_direction(Direction direction) {
    switch (direction) {
    case Direction::Left:
        return Direction::Right;
    case Direction::Right:
        return Direction::Left;
    case Direction::Up:
        return Direction::Down;
    case Direction::Down:
        return Direction::Up;
    default:
        return direction;
    }
}

} // namespace

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

        const auto moveAcrossMonitor = [&](PHLMONITOR targetMonitor) {
            if (!currentWindow || !sourceMonitor || !targetMonitor)
                return false;

            const auto workspaceId = CanvasLayoutInternal::preferred_workspace_id(targetMonitor, workspace);
            const auto targetWorkspace = g_pCompositor->getWorkspaceByID(workspaceId);
            const auto selector = workspace_selector(targetWorkspace);
            const auto moveDispatcher = g_pKeybindManager->m_dispatchers.find("movetoworkspacesilent");
            if (selector.empty() || moveDispatcher == g_pKeybindManager->m_dispatchers.end())
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

            const auto payload = lane->extract_active_window_payload();
            if (!payload)
                return true;

            const auto insertDirection = opposite_direction(direction);
            targetLayout->rememberManualCrossMonitorInsertion(currentWindow);

            moveDispatcher->second(selector);
            targetLane = targetAnchorWindow ? targetLayout->getLaneForWindow(targetAnchorWindow) : nullptr;
            if (!targetLane)
                targetLane = targetLayout->getActiveLane();
            if (!targetLane) {
                const auto targetMode =
                    targetMonitor && targetMonitor->m_size.x >= targetMonitor->m_size.y ? Mode::Row : Mode::Column;
                targetLane = targetLayout->ensureActiveLane(targetMonitor, targetMode);
            }

            if (targetAnchorWindow && targetLane->has_window(targetAnchorWindow) && !targetLane->is_active(targetAnchorWindow))
                targetLane->focus_window(targetAnchorWindow);
            targetLane->insert_window_payload(payload, insertDirection);
            targetLayout->forgetManualCrossMonitorInsertion(currentWindow);
            targetLayout->setActiveLane(targetLane);

            if (!dropEmptyLane(getLaneNode(lane), nullptr, sourceMonitor))
                relayoutVisibleCanvas(sourceMonitor);

            targetLayout->relayoutVisibleCanvas(targetMonitor);
            targetLayout->suppressNextWorkspaceFocusSync = true;
            CanvasLayoutInternal::switch_to_window(currentWindow, true);
            return true;
        };

        const auto betweenLanes = CanvasLayoutInternal::direction_moves_between_lanes(mode, direction);
        if (!betweenLanes) {
            const auto monitorDirection = CanvasLayoutInternal::direction_to_math(direction);
            const auto targetMonitor = sourceMonitor && monitorDirection ? g_pCompositor->getMonitorInDirection(sourceMonitor, *monitorDirection) : nullptr;
            if (!lane->active_item_at_edge(direction) || !targetMonitor) {
                lane->move_active_stack(direction);
                CanvasLayoutInternal::switch_to_window(lane->get_active_window());
                return;
            }

            if (moveAcrossMonitor(targetMonitor))
                return;

            lane->move_active_stack(direction);
            CanvasLayoutInternal::switch_to_window(lane->get_active_window());
            return;
        }

        if (!currentWindow || !sourceMonitor) {
            CanvasLayoutInternal::dispatch_directional_builtin("movewindow", direction);
            return;
        }

        const auto handoffPlan = CanvasLayoutInternal::plan_directional_handoff(lanes, activeLane, sourceMonitor, mode, direction, true);
        if (handoffPlan.route == CanvasLayoutInternal::DirectionalHandoffRoute::AdjacentLane) {
            const auto payload = lane->extract_active_window_payload();
            if (!payload)
                return;

            auto targetLaneNode = handoffPlan.targetLaneNode;
            targetLaneNode->data()->insert_window_payload(payload, direction);
            activeLane = targetLaneNode;
            finishLaneTransfer(getLaneNode(lane), sourceMonitor, false, true);
            return;
        }

        if (handoffPlan.route == CanvasLayoutInternal::DirectionalHandoffRoute::CrossMonitor) {
            if (!moveAcrossMonitor(handoffPlan.targetMonitor))
                CanvasLayoutInternal::dispatch_directional_builtin("movewindow", direction);
            return;
        }

        if (handoffPlan.route != CanvasLayoutInternal::DirectionalHandoffRoute::CreateLane)
            return;

        const auto payload = lane->extract_active_window_payload();
        if (!payload)
            return;

        auto sourceLaneNode = activeLane;
        auto *newLane = new Lane(sourceMonitor, mode);
        auto newLaneNode = insertLaneNode(newLane, direction, sourceLaneNode);

        newLane->insert_window_payload(payload, direction);
        activeLane = newLaneNode;
        finishLaneTransfer(sourceLaneNode, sourceMonitor, false, true);
        return;
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
    const auto syncPolicy = suppressNextWorkspaceFocusSync ? ActiveLaneSyncPolicy::None : ActiveLaneSyncPolicy::WorkspaceFocus;
    suppressNextWorkspaceFocusSync = false;
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

        activeLane = newLaneNode;
        finishLaneTransfer(currentLaneNode, nullptr, false, true);
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
