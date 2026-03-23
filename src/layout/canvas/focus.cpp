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
const char* direction_dispatch_arg(Direction direction) {
    switch (direction) {
        case Direction::Left:
            return "l";
        case Direction::Right:
            return "r";
        case Direction::Up:
            return "u";
        case Direction::Down:
            return "d";
        case Direction::Begin:
            return "b";
        case Direction::End:
            return "e";
        default:
            return nullptr;
    }
}

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

ListNode<Lane*>* edge_lane_anchor(List<Lane*>& lanes, Mode mode, Direction direction) {
    if (lanes.empty())
        return nullptr;

    if (direction_inserts_before_current(mode, direction))
        return lanes.first();

    return lanes.last();
}

bool should_sync_workspace_focus_before_move(ListNode<Lane*>* activeLaneNode) {
    if (!activeLaneNode || !activeLaneNode->data())
        return true;

    const auto lane = activeLaneNode->data();
    return !(lane->is_ephemeral() && lane->empty());
}

DirectionalHandoffPlan plan_directional_handoff(List<Lane*>& lanes, ListNode<Lane*>* current, PHLMONITOR sourceMonitor, Mode mode, Direction direction, bool allow_create) {
    if (!direction_moves_between_lanes(mode, direction))
        return {};

    if (auto targetLaneNode = adjacent_lane(current, mode, direction)) {
        return {
            .route = DirectionalHandoffRoute::AdjacentLane,
            .targetLaneNode = targetLaneNode,
        };
    }

    const auto monitorDirection = direction_to_math(direction);
    if (sourceMonitor && monitorDirection) {
        if (auto targetMonitor = g_pCompositor->getMonitorInDirection(sourceMonitor, *monitorDirection)) {
            return {
                .route = DirectionalHandoffRoute::CrossMonitor,
                .targetMonitor = targetMonitor,
            };
        }
    }

    if (allow_create) {
        return {
            .route = DirectionalHandoffRoute::CreateLane,
            .targetLaneNode = edge_lane_anchor(lanes, mode, direction),
        };
    }

    return {};
}

void dispatch_directional_builtin(const char* dispatcher, Direction direction) {
    const auto arg = direction_dispatch_arg(direction);
    if (!arg)
        return;

    const auto it = g_pKeybindManager->m_dispatchers.find(dispatcher);
    if (it == g_pKeybindManager->m_dispatchers.end())
        return;

    it->second(arg);
}

void dispatch_builtin_movefocus(Direction direction) {
    dispatch_directional_builtin("movefocus", direction);
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
    const auto beforeLane = activeLane ? activeLane->data() : nullptr;
    const auto beforeWindow = beforeLane ? beforeLane->get_active_window() : nullptr;
    const auto beforeLaneIndex = laneIndexOf(beforeLane);
    const auto totalLanes = static_cast<int>(laneCount());

    if (window == nullptr) {
        spdlog::debug("onWindowFocusChange: ignored null window canvas_ws={} lanes={} before_lane={} before_lane_index={} before_window={}",
                      CanvasLayoutInternal::get_workspace_id(),
                      totalLanes,
                      static_cast<const void*>(beforeLane),
                      beforeLaneIndex,
                      static_cast<const void*>(beforeWindow ? beforeWindow.get() : nullptr));
        return;
    }

    auto s = getLaneForWindow(window);
    const auto targetWindow = s ? s->get_active_window() : nullptr;
    const auto targetLaneIndex = laneIndexOf(s);
    spdlog::info(
        "onWindowFocusChange: window={} workspace={} monitor={} canvas_ws={} lane_found={} lanes={} before_lane={} before_lane_index={} before_window={} target_lane={} target_lane_index={} target_window={} same_lane={}",
        static_cast<const void*>(window.get()),
        window->workspaceID(),
        window->monitorID(),
        CanvasLayoutInternal::get_workspace_id(),
        s != nullptr,
        totalLanes,
        static_cast<const void*>(beforeLane),
        beforeLaneIndex,
        static_cast<const void*>(beforeWindow ? beforeWindow.get() : nullptr),
        static_cast<const void*>(s),
        targetLaneIndex,
        static_cast<const void*>(targetWindow ? targetWindow.get() : nullptr),
        beforeLane == s);

    if (s == nullptr) {
        spdlog::warn("onWindowFocusChange: window={} not managed by current canvas canvas_ws={} lanes={}",
                     static_cast<const void*>(window.get()),
                     CanvasLayoutInternal::get_workspace_id(),
                     totalLanes);
        return;
    }

    setActiveLane(s);
    s->focus_window(window);

    const auto afterLane = activeLane ? activeLane->data() : nullptr;
    const auto afterWindow = afterLane ? afterLane->get_active_window() : nullptr;
    spdlog::info("onWindowFocusChange: synced window={} canvas_ws={} after_lane={} after_lane_index={} after_window={}",
                 static_cast<const void*>(window.get()),
                 CanvasLayoutInternal::get_workspace_id(),
                 static_cast<const void*>(afterLane),
                 laneIndexOf(afterLane),
                 static_cast<const void*>(afterWindow ? afterWindow.get() : nullptr));
}

bool CanvasLayout::adoptFocusedLane(PHLWINDOW focusedWindow, PHLMONITOR fallbackMonitor)
{
    if (!focusedWindow)
        return false;

    const auto currentLane = activeLane ? activeLane->data() : nullptr;
    const auto currentWindow = currentLane ? currentLane->get_active_window() : nullptr;
    auto targetLane = getLaneForWindow(focusedWindow);
    if (!targetLane)
        return false;

    if (activeLane && currentLane && currentLane->is_ephemeral() && currentLane->empty() && targetLane != currentLane)
        dropEmptyLane(activeLane, targetLane, fallbackMonitor, true);

    if (currentWindow == focusedWindow)
        return true;

    onWindowFocusChange(focusedWindow);
    return true;
}

void CanvasLayout::syncActiveStateFromWorkspaceFocus()
{
    const auto workspace = getCanvasWorkspace();
    if (!workspace)
        return;

    const auto focusedWindow = workspace->getLastFocusedWindow();
    const auto currentLane = activeLane ? activeLane->data() : nullptr;
    const auto currentWindow = currentLane ? currentLane->get_active_window() : nullptr;
    const auto managed = focusedWindow && getLaneForWindow(focusedWindow) != nullptr;

    spdlog::debug("syncActiveStateFromWorkspaceFocus: canvas_ws={} focused_window={} focused_workspace={} focused_monitor={} managed={} current_window={}",
                  workspace->m_id,
                  static_cast<const void*>(focusedWindow ? focusedWindow.get() : nullptr),
                  focusedWindow ? focusedWindow->workspaceID() : WORKSPACE_INVALID,
                  focusedWindow ? focusedWindow->monitorID() : MONITOR_INVALID,
                  managed,
                  static_cast<const void*>(currentWindow ? currentWindow.get() : nullptr));

    if (!managed || currentWindow == focusedWindow)
        return;

    if (adoptFocusedLane(focusedWindow, getVisibleCanvasMonitor()))
        spdlog::info("syncActiveStateFromWorkspaceFocus: adopted focused lane canvas_ws={} focused_window={}",
                     workspace->m_id,
                     static_cast<const void*>(focusedWindow.get()));
}

void CanvasLayout::moveWindowTo(PHLWINDOW window, const std::string &direction, bool)
{
    auto s = getLaneForWindow(window);
    if (s == nullptr || !s->is_active(window))
        return;

    onWindowFocusChange(window);

    switch (direction.at(0)) {
        case 'l': move_window(window->workspaceID(), Direction::Left); break;
        case 'r': move_window(window->workspaceID(), Direction::Right); break;
        case 'u': move_window(window->workspaceID(), Direction::Up); break;
        case 'd': move_window(window->workspaceID(), Direction::Down); break;
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
    if (CanvasLayoutInternal::should_sync_workspace_focus_before_move(activeLane))
        syncActiveStateFromWorkspaceFocus();
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
        if (targetLayout)
            targetLayout->syncActiveStateFromWorkspaceFocus();
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

        if (dropEmptyLane(sourceLaneNode, nullptr, beforeMonitor, true)) {
            spdlog::info("move_focus: dropped empty lane after leaving workspace={} direction={}",
                         workspace, CanvasLayoutInternal::direction_name(direction));
        }

        if (targetLane != nullptr)
            targetLayout->setActiveLane(targetLane);
        if (targetLane != nullptr)
            targetLane->focus_window(crossMonitorTarget);
        else
            spdlog::warn("move_focus: no lane for crossed monitor target window={} workspace={}",
                         static_cast<const void*>(crossMonitorTarget.get()), workspaceId);

        if (targetLayout != nullptr && targetLane != nullptr) {
            const auto targetWorkspace = targetLayout->getCanvasWorkspace();
            auto targetMonitor = targetWorkspace ? CanvasLayoutInternal::visible_monitor_for_workspace(targetWorkspace) : monitor;
            if (targetMonitor)
                targetLayout->relayoutCanvas(targetMonitor, targetWorkspace && !targetWorkspace->m_isSpecialWorkspace);
        }

        spdlog::info("move_focus: workspace={} direction={} after={} result={}",
                     workspace,
                     CanvasLayoutInternal::direction_name(direction),
                     static_cast<const void*>(crossMonitorTarget.get()),
                     focus_move_result_name(FocusMoveResult::CrossMonitor));
        CanvasLayoutInternal::switch_to_window(crossMonitorTarget, true);
    };

        const auto mode = s->get_mode();
    const auto moveAcrossLanesOrCreate = [&](bool allowCreate) {
        const auto handoffPlan = CanvasLayoutInternal::plan_directional_handoff(lanes, activeLane, beforeMonitor, mode, direction, allowCreate);
        if (handoffPlan.route == CanvasLayoutInternal::DirectionalHandoffRoute::AdjacentLane) {
            auto targetLaneNode = handoffPlan.targetLaneNode;
            activeLane = targetLaneNode;
            if (dropEmptyLane(sourceLaneNode, activeLane ? activeLane->data() : nullptr, beforeMonitor, true)) {
                spdlog::info("move_focus: dropped empty lane after leaving workspace={} direction={}",
                             workspace, CanvasLayoutInternal::direction_name(direction));
            } else {
                relayoutVisibleCanvas(beforeMonitor);
            }

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

        if (handoffPlan.route == CanvasLayoutInternal::DirectionalHandoffRoute::CrossMonitor) {
            handoffAcrossMonitor(handoffPlan.targetMonitor);
            return;
        }

        if (handoffPlan.route != CanvasLayoutInternal::DirectionalHandoffRoute::CreateLane)
            return;

        auto newLane = new Lane(beforeMonitor, mode);
        newLane->set_ephemeral(true);
        lanes.push_back(newLane);
        auto newLaneNode = lanes.last();
        const auto edgeAnchor = handoffPlan.targetLaneNode;
        if (edgeAnchor && edgeAnchor != newLaneNode) {
            if (CanvasLayoutInternal::direction_inserts_before_current(mode, direction))
                lanes.move_before(edgeAnchor, newLaneNode);
            else
                lanes.move_after(edgeAnchor, newLaneNode);
        }

        activeLane = newLaneNode;
        relayoutVisibleCanvas(beforeMonitor);
        spdlog::info("move_focus: created empty lane workspace={} direction={} lane={}",
                     workspace,
                     CanvasLayoutInternal::direction_name(direction),
                     static_cast<const void*>(newLane));
        return;
    };

    if (s->empty()) {
        if (CanvasLayoutInternal::direction_moves_between_lanes(mode, direction))
            moveAcrossLanesOrCreate(false);
        return;
    }

    const auto moveResult = s->move_focus(direction, **focus_wrap != 0);
    if (moveResult != FocusMoveResult::Moved && CanvasLayoutInternal::direction_moves_between_lanes(mode, direction)) {
        moveAcrossLanesOrCreate(true);
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
            moveAcrossLanesOrCreate(true);
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
