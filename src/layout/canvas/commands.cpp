#include <string>

#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/managers/KeybindManager.hpp>

#include "../lane/lane.h"
#include "layout.h"
#include "internal.h"

namespace {
std::string workspace_selector(PHLWORKSPACE workspace) {
    if (!workspace)
        return {};

    if (!workspace->m_name.empty())
        return workspace->m_name;

    return std::to_string(workspace->m_id);
}

void dispatch_builtin_movewindow(Direction direction) {
    const auto it = g_pKeybindManager->m_dispatchers.find("movewindow");
    if (it == g_pKeybindManager->m_dispatchers.end())
        return;

    switch (direction) {
    case Direction::Left:
        it->second("l");
        return;
    case Direction::Right:
        it->second("r");
        return;
    case Direction::Up:
        it->second("u");
        return;
    case Direction::Down:
        it->second("d");
        return;
    case Direction::Begin:
        it->second("b");
        return;
    case Direction::End:
        it->second("e");
        return;
    default:
        return;
    }
}
} // namespace

void CanvasLayout::cycle_window_size(int workspace, int step)
{
    (void)workspace;
    withActiveLane(ActiveLaneSyncPolicy::WorkspaceFocus, [step](Lane *lane) {
        lane->resize_active_stack(step);
    });
}

void CanvasLayout::move_window(int workspace, Direction direction) {
    (void)workspace;
    withActiveLane(ActiveLaneSyncPolicy::WorkspaceFocus, [&](Lane *lane) {
        const auto mode = lane->get_mode();
        if (!CanvasLayoutInternal::direction_moves_between_lanes(mode, direction)) {
            lane->move_active_stack(direction);
            CanvasLayoutInternal::switch_to_window(lane->get_active_window());
            return;
        }

        const auto currentWindow = lane->get_active_window();
        const auto sourceMonitor = currentWindow ? g_pCompositor->getMonitorFromID(currentWindow->monitorID()) : getVisibleCanvasMonitor();
        if (!currentWindow || !sourceMonitor) {
            dispatch_builtin_movewindow(direction);
            return;
        }

        if (auto targetLaneNode = CanvasLayoutInternal::adjacent_lane(activeLane, mode, direction)) {
            StackWidth width = StackWidth::OneHalf;
            double maxw = 0.0;
            auto *window = lane->extract_active_window(&width, &maxw);
            if (!window)
                return;

            targetLaneNode->data()->insert_window(window, width, maxw, direction);
            activeLane = targetLaneNode;

            if (lane->empty()) {
                auto *doomedNode = getLaneNode(lane);
                if (doomedNode && doomedNode != activeLane) {
                    lanes.erase(doomedNode);
                    delete lane;
                }
            }

            relayoutVisibleCanvas(sourceMonitor);
            CanvasLayoutInternal::switch_to_window(activeLane->data()->get_active_window(), true);
            return;
        }

        const auto monitorDirection = CanvasLayoutInternal::direction_to_math(direction);
        if (const auto targetMonitor = monitorDirection ? g_pCompositor->getMonitorInDirection(sourceMonitor, *monitorDirection) : nullptr) {
            const auto workspaceId = CanvasLayoutInternal::preferred_workspace_id(targetMonitor, workspace);
            const auto targetWorkspace = g_pCompositor->getWorkspaceByID(workspaceId);
            const auto selector = workspace_selector(targetWorkspace);
            const auto moveDispatcher = g_pKeybindManager->m_dispatchers.find("movetoworkspacesilent");
            if (selector.empty() || moveDispatcher == g_pKeybindManager->m_dispatchers.end()) {
                dispatch_builtin_movewindow(direction);
                return;
            }

            if (auto *targetLayout = CanvasLayoutInternal::get_canvas_for_workspace(workspaceId)) {
                targetLayout->syncActiveStateFromWorkspaceFocus();
                if (!targetLayout->getActiveLane()) {
                    auto *newLane = new Lane(targetMonitor, mode);
                    targetLayout->lanes.push_back(newLane);
                    targetLayout->activeLane = targetLayout->lanes.last();
                }
            }

            moveDispatcher->second(selector);
            CanvasLayoutInternal::switch_to_window(currentWindow, true);
            return;
        }

        StackWidth width = StackWidth::OneHalf;
        double maxw = 0.0;
        auto *window = lane->extract_active_window(&width, &maxw);
        if (!window)
            return;

        auto sourceLaneNode = activeLane;
        auto *newLane = new Lane(sourceMonitor, mode);
        lanes.push_back(newLane);
        auto newLaneNode = lanes.last();
        if (sourceLaneNode && sourceLaneNode != newLaneNode) {
            if (CanvasLayoutInternal::direction_inserts_before_current(mode, direction))
                lanes.move_before(sourceLaneNode, newLaneNode);
            else
                lanes.move_after(sourceLaneNode, newLaneNode);
        }

        newLane->insert_window(window, width, maxw, direction);
        activeLane = newLaneNode;

        if (lane->empty()) {
            auto *doomedNode = getLaneNode(lane);
            if (doomedNode && doomedNode != activeLane) {
                lanes.erase(doomedNode);
                delete lane;
            }
        }

        relayoutVisibleCanvas(sourceMonitor);
        CanvasLayoutInternal::switch_to_window(activeLane->data()->get_active_window(), true);
    });
}

void CanvasLayout::align_window(int workspace, Direction direction) {
    (void)workspace;
    withActiveLane(ActiveLaneSyncPolicy::WorkspaceFocus, [direction](Lane *lane) {
        lane->align_stack(direction);
    });
}

void CanvasLayout::admit_window_left(int workspace) {
    (void)workspace;
    withActiveLane(ActiveLaneSyncPolicy::WorkspaceFocus, [](Lane *lane) {
        lane->admit_window_left();
    });
}

void CanvasLayout::expel_window_right(int workspace) {
    (void)workspace;
    withActiveLane(ActiveLaneSyncPolicy::WorkspaceFocus, [](Lane *lane) {
        lane->expel_window_right();
    });
}

void CanvasLayout::set_mode(int workspace, Mode mode) {
    (void)workspace;
    withActiveLane(ActiveLaneSyncPolicy::WorkspaceFocus, [mode](Lane *lane) {
        lane->set_mode(mode);
    });
}

void CanvasLayout::fit_size(int workspace, FitSize fitsize) {
    (void)workspace;
    withActiveLane(ActiveLaneSyncPolicy::WorkspaceFocus, [fitsize](Lane *lane) {
        lane->fit_size(fitsize);
    });
}

void CanvasLayout::toggle_overview(int workspace) {
    (void)workspace;
    withActiveLane(ActiveLaneSyncPolicy::WorkspaceFocus, [](Lane *lane) {
        lane->toggle_overview();
    });
}

void CanvasLayout::toggle_fullscreen(int workspace) {
    (void)workspace;
    withActiveLane(ActiveLaneSyncPolicy::WorkspaceFocus, [](Lane *lane) {
        lane->toggle_fullscreen_active_window();
    });
}

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
        lanes.push_back(newLane);
        auto newLaneNode = lanes.last();

        if (direction == Direction::Left || direction == Direction::Up || direction == Direction::Begin)
            lanes.move_before(currentLaneNode, newLaneNode);
        else if (currentLaneNode != newLaneNode)
            lanes.move_after(currentLaneNode, newLaneNode);

        activeLane = newLaneNode;

        if (lane->empty()) {
            lanes.erase(currentLaneNode);
            delete lane;
        }

        relayoutVisibleCanvas();

        if (const auto window = newLane->get_active_window())
            CanvasLayoutInternal::switch_to_window(window, true);
    });
}

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
