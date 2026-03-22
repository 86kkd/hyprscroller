#include <cstdio>

#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/config/ConfigValue.hpp>
#include <hyprland/src/helpers/Monitor.hpp>
#include <hyprland/src/managers/KeybindManager.hpp>
#include <hyprland/src/plugins/PluginAPI.hpp>
#include <spdlog/spdlog.h>

#include "internal.h"

extern HANDLE PHANDLE;

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

ListNode<Lane*>* adjacent_lane(ListNode<Lane*>* current, Mode mode, Direction direction) {
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

void dispatch_builtin_movefocus(Direction direction) {
    switch (direction) {
        case Direction::Left:
            g_pKeybindManager->m_dispatchers["movefocus"]("l");
            return;
        case Direction::Right:
            g_pKeybindManager->m_dispatchers["movefocus"]("r");
            return;
        case Direction::Up:
            g_pKeybindManager->m_dispatchers["movefocus"]("u");
            return;
        case Direction::Down:
            g_pKeybindManager->m_dispatchers["movefocus"]("d");
            return;
        default:
            return;
    }
}

void focus_window_monitor(PHLWINDOW window) {
    if (!window)
        return;

    const auto targetMonitor = g_pCompositor->getMonitorFromID(window->monitorID());
    const auto currentMonitor = g_pCompositor->getMonitorFromCursor();
    if (!targetMonitor || !currentMonitor || targetMonitor == currentMonitor || targetMonitor->m_name.empty())
        return;

    const auto focusMonitor = g_pKeybindManager->m_dispatchers.find("focusmonitor");
    if (focusMonitor == g_pKeybindManager->m_dispatchers.end())
        return;

    spdlog::debug("switch_to_window: focusing monitor={} before window={} workspace={}",
                  targetMonitor->m_name,
                  static_cast<const void*>(window.get()),
                  window->workspaceID());
    focusMonitor->second(targetMonitor->m_name);
}

void switch_to_window(PHLWINDOW window, bool warp_cursor)
{
    if (!window)
        return;

    focus_window_monitor(window);

    if (!g_pCompositor->isWindowActive(window)) {
        spdlog::debug("switch_to_window: focusing window={} workspace={}",
                      static_cast<const void*>(window.get()), window->workspaceID());
        char selector[64];
        std::snprintf(selector, sizeof(selector), "address:0x%lx",
                      reinterpret_cast<unsigned long>(window.get()));
        g_pKeybindManager->m_dispatchers["focuswindow"](selector);
    }

    if (warp_cursor)
        window->warpCursor(true);
}
} // namespace CanvasLayoutInternal

void CanvasLayout::onWindowFocusChange(PHLWINDOW window)
{
    if (window == nullptr)
        return;

    auto s = getLaneForWindow(window);
    if (s == nullptr)
        return;

    setActiveLane(s);
    s->focus_window(window);
}

void CanvasLayout::moveWindowTo(PHLWINDOW window, const std::string &direction, bool)
{
    auto s = getLaneForWindow(window);
    if (s == nullptr || !s->is_active(window))
        return;

    switch (direction.at(0)) {
        case 'l': s->move_active_stack(Direction::Left); break;
        case 'r': s->move_active_stack(Direction::Right); break;
        case 'u': s->move_active_stack(Direction::Up); break;
        case 'd': s->move_active_stack(Direction::Down); break;
        default: break;
    }
}

void CanvasLayout::move_focus(int workspace, Direction direction)
{
    const auto focus_move_result_name = [](FocusMoveResult result) {
        switch (result) {
        case FocusMoveResult::Moved:
            return "moved";
        case FocusMoveResult::NoOp:
            return "noop";
        case FocusMoveResult::CrossMonitor:
            return "cross_monitor";
        }

        return "unknown";
    };

    static auto* const *focus_wrap = (Hyprlang::INT* const *)HyprlandAPI::getConfigValue(PHANDLE, "plugin:scroller:focus_wrap")->getDataStaticPtr();
    auto s = getActiveLane();
    const auto before = s ? s->get_active_window() : nullptr;
    const auto beforeMonitor = before ? g_pCompositor->getMonitorFromID(before->monitorID()) : monitorFromPointingOrCursor();
    const auto beforeActiveWorkspaceId = beforeMonitor ? beforeMonitor->activeWorkspaceID() : WORKSPACE_INVALID;
    const auto beforeSpecialWorkspaceId = beforeMonitor ? beforeMonitor->activeSpecialWorkspaceID() : WORKSPACE_INVALID;
    auto sourceLane = s;
    auto sourceLaneNode = activeLane;
    spdlog::info("move_focus: workspace={} direction={} lane_found={} before={}",
                 workspace, CanvasLayoutInternal::direction_name(direction), s != nullptr,
                 static_cast<const void*>(before ? before.get() : nullptr));
    if (s == nullptr) {
        CanvasLayoutInternal::dispatch_builtin_movefocus(direction);
        return;
    }

    const auto relayoutCurrentCanvas = [&](PHLMONITOR fallbackMonitor) {
        const auto workspaceHandle = getCanvasWorkspace();
        auto monitor = workspaceHandle ? CanvasLayoutInternal::visible_monitor_for_workspace(workspaceHandle) : fallbackMonitor;
        if (monitor)
            relayoutCanvas(monitor, workspaceHandle && !workspaceHandle->m_isSpecialWorkspace);
    };

    const auto dropEmptySourceLane = [&](PHLMONITOR fallbackMonitor) {
        if (!sourceLaneNode || !sourceLane || !sourceLane->empty())
            return;

        auto doomed = sourceLane;
        if (activeLane == sourceLaneNode)
            activeLane = sourceLaneNode->next() ? sourceLaneNode->next() : sourceLaneNode->prev();
        lanes.erase(sourceLaneNode);
        delete doomed;
        relayoutCurrentCanvas(fallbackMonitor);
        spdlog::info("move_focus: dropped empty lane after leaving workspace={} direction={}",
                     workspace, CanvasLayoutInternal::direction_name(direction));
    };

    const auto handoffAcrossMonitor = [&](PHLMONITOR monitor) {
        const auto activeWorkspaceId = monitor ? monitor->activeWorkspaceID() : WORKSPACE_INVALID;
        const auto workspaceId = CanvasLayoutInternal::preferred_workspace_id(monitor, workspace);
        const auto specialWorkspaceId = monitor ? monitor->activeSpecialWorkspaceID() : WORKSPACE_INVALID;
        spdlog::info(
            "move_focus_cross_monitor: source_workspace={} direction={} source_window={} "
            "source_active_ws={} source_special_ws={} dest_active_ws={} dest_special_ws={} selected_ws={}",
            workspace,
            CanvasLayoutInternal::direction_name(direction),
            static_cast<const void*>(before ? before.get() : nullptr),
            beforeActiveWorkspaceId,
            beforeSpecialWorkspaceId,
            activeWorkspaceId,
            specialWorkspaceId,
            workspaceId);
        if (!monitor) {
            spdlog::warn("move_focus: no monitor in direction={} from workspace={}",
                         CanvasLayoutInternal::direction_name(direction), workspace);
            return;
        }

        const char* targetSelection = "geometry";
        auto targetLayout = CanvasLayoutInternal::get_canvas_for_workspace(workspaceId);
        auto targetLane = targetLayout ? targetLayout->getActiveLane() : nullptr;
        auto crossMonitorTarget = targetLane ? targetLane->get_active_window() : nullptr;
        if (crossMonitorTarget)
            targetSelection = "active";
        else
            crossMonitorTarget = CanvasLayoutInternal::pick_cross_monitor_target_window(monitor, workspaceId, direction, before);
        if (!crossMonitorTarget) {
            spdlog::warn("move_focus: no target window for crossed monitor workspace={}", workspaceId);
            return;
        }

        if (!targetLane || !targetLane->has_window(crossMonitorTarget))
            targetLane = targetLayout ? targetLayout->getLaneForWindow(crossMonitorTarget) : nullptr;
        spdlog::info(
            "move_focus_cross_monitor_target: selected_ws={} target_window={} target_workspace={} "
            "target_layout_found={} target_lane_found={} selection={} target_pos=({}, {}) target_size=({}, {})",
            workspaceId,
            static_cast<const void*>(crossMonitorTarget ? crossMonitorTarget.get() : nullptr),
            crossMonitorTarget ? crossMonitorTarget->workspaceID() : WORKSPACE_INVALID,
            targetLayout != nullptr,
            targetLane != nullptr,
            targetSelection,
            crossMonitorTarget ? crossMonitorTarget->m_position.x : 0.0,
            crossMonitorTarget ? crossMonitorTarget->m_position.y : 0.0,
            crossMonitorTarget ? crossMonitorTarget->m_size.x : 0.0,
            crossMonitorTarget ? crossMonitorTarget->m_size.y : 0.0);

        dropEmptySourceLane(beforeMonitor);

        if (targetLane != nullptr)
            targetLayout->setActiveLane(targetLane);
        if (targetLane != nullptr)
            targetLane->focus_window(crossMonitorTarget);
        else
            spdlog::warn("move_focus: no lane for crossed monitor target window={} workspace={}",
                         static_cast<const void*>(crossMonitorTarget.get()), workspaceId);

        spdlog::info("move_focus: workspace={} direction={} after={} result={}",
                     workspace,
                     CanvasLayoutInternal::direction_name(direction),
                     static_cast<const void*>(crossMonitorTarget.get()),
                     focus_move_result_name(FocusMoveResult::CrossMonitor));
        CanvasLayoutInternal::switch_to_window(crossMonitorTarget, true);
    };

    const auto mode = s->get_mode();
    const auto moveAcrossLanesOrCreate = [&]() {
        if (auto targetLaneNode = CanvasLayoutInternal::adjacent_lane(activeLane, mode, direction)) {
            activeLane = targetLaneNode;
            dropEmptySourceLane(beforeMonitor);
            relayoutCurrentCanvas(beforeMonitor);

            const auto targetWindow = activeLane->data()->get_active_window();
            spdlog::info("move_focus: workspace={} direction={} lane_switch={} target_window={}",
                         workspace,
                         CanvasLayoutInternal::direction_name(direction),
                         true,
                         static_cast<const void*>(targetWindow ? targetWindow.get() : nullptr));
            if (targetWindow)
                CanvasLayoutInternal::switch_to_window(targetWindow, true);
            return;
        }

        const auto monitorDirection = CanvasLayoutInternal::direction_to_math(direction);
        auto monitor = beforeMonitor && monitorDirection ? g_pCompositor->getMonitorInDirection(beforeMonitor, *monitorDirection) : nullptr;
        if (monitor) {
            handoffAcrossMonitor(monitor);
            return;
        }

        if (s->empty())
            return;

        auto newLane = new Lane(beforeMonitor, mode);
        newLane->set_ephemeral(true);
        lanes.push_back(newLane);
        auto newLaneNode = lanes.last();
        if (CanvasLayoutInternal::direction_inserts_before_current(mode, direction))
            lanes.move_before(sourceLaneNode, newLaneNode);
        else if (sourceLaneNode != newLaneNode)
            lanes.move_after(sourceLaneNode, newLaneNode);

        activeLane = newLaneNode;
        relayoutCurrentCanvas(beforeMonitor);
        spdlog::info("move_focus: created empty lane workspace={} direction={} lane={}",
                     workspace,
                     CanvasLayoutInternal::direction_name(direction),
                     static_cast<const void*>(newLane));
        return;
    };

    if (s->empty()) {
        if (CanvasLayoutInternal::direction_moves_between_lanes(mode, direction))
            moveAcrossLanesOrCreate();
        return;
    }

    const auto moveResult = s->move_focus(direction, **focus_wrap != 0);
    if (moveResult != FocusMoveResult::Moved && CanvasLayoutInternal::direction_moves_between_lanes(mode, direction)) {
        moveAcrossLanesOrCreate();
        return;
    }

    if (moveResult == FocusMoveResult::CrossMonitor) {
        const auto monitorDirection = CanvasLayoutInternal::direction_to_math(direction);
        auto monitor = beforeMonitor && monitorDirection ? g_pCompositor->getMonitorInDirection(beforeMonitor, *monitorDirection) : nullptr;
        handoffAcrossMonitor(monitor);
        return;
    }

    if (moveResult == FocusMoveResult::NoOp) {
        if (CanvasLayoutInternal::direction_moves_between_lanes(mode, direction))
            moveAcrossLanesOrCreate();
        return;
    }

    const auto after = s->get_active_window();
    spdlog::info("move_focus: workspace={} direction={} after={} result={}",
                 workspace,
                 CanvasLayoutInternal::direction_name(direction),
                 static_cast<const void*>(after ? after.get() : nullptr),
                 focus_move_result_name(moveResult));

    setActiveLane(s);
    CanvasLayoutInternal::switch_to_window(s->get_active_window(), true);
}
