#include <cstdio>

#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/config/ConfigValue.hpp>
#include <hyprland/src/helpers/Monitor.hpp>
#include <hyprland/src/managers/KeybindManager.hpp>
#include <hyprland/src/plugins/PluginAPI.hpp>
#include <spdlog/spdlog.h>

#include "scroller_layout_internal.h"

extern HANDLE PHANDLE;

namespace ScrollerLayoutInternal {
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
} // namespace ScrollerLayoutInternal

void ScrollerLayout::onWindowFocusChange(PHLWINDOW window)
{
    if (window == nullptr)
        return;

    auto s = getRowForWindow(window);
    if (s == nullptr)
        return;

    s->focus_window(window);
}

void ScrollerLayout::moveWindowTo(PHLWINDOW window, const std::string &direction, bool)
{
    auto s = getRowForWindow(window);
    if (s == nullptr || !s->is_active(window))
        return;

    switch (direction.at(0)) {
        case 'l': s->move_active_column(Direction::Left); break;
        case 'r': s->move_active_column(Direction::Right); break;
        case 'u': s->move_active_column(Direction::Up); break;
        case 'd': s->move_active_column(Direction::Down); break;
        default: break;
    }
}

void ScrollerLayout::move_focus(int workspace, Direction direction)
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
    auto s = getRowForWorkspace(workspace);
    const auto before = s ? s->get_active_window() : nullptr;
    const auto beforeMonitor = before ? g_pCompositor->getMonitorFromID(before->monitorID()) : monitorFromPointingOrCursor();
    const auto beforeActiveWorkspaceId = beforeMonitor ? beforeMonitor->activeWorkspaceID() : WORKSPACE_INVALID;
    const auto beforeSpecialWorkspaceId = beforeMonitor ? beforeMonitor->activeSpecialWorkspaceID() : WORKSPACE_INVALID;
    spdlog::info("move_focus: workspace={} direction={} row_found={} before={}",
                 workspace, ScrollerLayoutInternal::direction_name(direction), s != nullptr,
                 static_cast<const void*>(before ? before.get() : nullptr));
    if (s == nullptr) {
        ScrollerLayoutInternal::dispatch_builtin_movefocus(direction);
        return;
    }

    const auto moveResult = s->move_focus(direction, **focus_wrap != 0);
    if (moveResult == FocusMoveResult::CrossMonitor) {
        const auto monitorDirection = ScrollerLayoutInternal::direction_to_math(direction);
        auto monitor = beforeMonitor && monitorDirection ? g_pCompositor->getMonitorInDirection(beforeMonitor, *monitorDirection) : nullptr;
        const auto activeWorkspaceId = monitor ? monitor->activeWorkspaceID() : WORKSPACE_INVALID;
        const auto workspaceId = ScrollerLayoutInternal::preferred_workspace_id(monitor, workspace);
        const auto specialWorkspaceId = monitor ? monitor->activeSpecialWorkspaceID() : WORKSPACE_INVALID;
        spdlog::info(
            "move_focus_cross_monitor: source_workspace={} direction={} source_window={} "
            "source_active_ws={} source_special_ws={} dest_active_ws={} dest_special_ws={} selected_ws={}",
            workspace,
            ScrollerLayoutInternal::direction_name(direction),
            static_cast<const void*>(before ? before.get() : nullptr),
            beforeActiveWorkspaceId,
            beforeSpecialWorkspaceId,
            activeWorkspaceId,
            specialWorkspaceId,
            workspaceId);
        if (!monitor) {
            spdlog::warn("move_focus: no monitor in direction={} from workspace={}",
                         ScrollerLayoutInternal::direction_name(direction), workspace);
            return;
        }

        const char* targetSelection = "geometry";
        auto targetLayout = ScrollerLayoutInternal::get_scroller_for_workspace(workspaceId);
        auto targetRow = targetLayout ? targetLayout->getRowForWorkspace(workspaceId) : nullptr;
        auto crossMonitorTarget = targetRow ? targetRow->get_active_window() : nullptr;
        if (crossMonitorTarget)
            targetSelection = "active";
        else
            crossMonitorTarget = ScrollerLayoutInternal::pick_cross_monitor_target_window(monitor, workspaceId, direction, before);
        if (!crossMonitorTarget) {
            spdlog::warn("move_focus: no target window for crossed monitor workspace={}", workspaceId);
            return;
        }

        if (!targetRow || !targetRow->has_window(crossMonitorTarget))
            targetRow = targetLayout ? targetLayout->getRowForWindow(crossMonitorTarget) : nullptr;
        spdlog::info(
            "move_focus_cross_monitor_target: selected_ws={} target_window={} target_workspace={} "
            "target_layout_found={} target_row_found={} selection={} target_pos=({}, {}) target_size=({}, {})",
            workspaceId,
            static_cast<const void*>(crossMonitorTarget ? crossMonitorTarget.get() : nullptr),
            crossMonitorTarget ? crossMonitorTarget->workspaceID() : WORKSPACE_INVALID,
            targetLayout != nullptr,
            targetRow != nullptr,
            targetSelection,
            crossMonitorTarget ? crossMonitorTarget->m_position.x : 0.0,
            crossMonitorTarget ? crossMonitorTarget->m_position.y : 0.0,
            crossMonitorTarget ? crossMonitorTarget->m_size.x : 0.0,
            crossMonitorTarget ? crossMonitorTarget->m_size.y : 0.0);

        if (targetRow != nullptr)
            targetRow->focus_window(crossMonitorTarget);
        else
            spdlog::warn("move_focus: no row for crossed monitor target window={} workspace={}",
                         static_cast<const void*>(crossMonitorTarget.get()), workspaceId);

        spdlog::info("move_focus: workspace={} direction={} after={} result={}",
                     workspace,
                     ScrollerLayoutInternal::direction_name(direction),
                     static_cast<const void*>(crossMonitorTarget.get()),
                     focus_move_result_name(moveResult));
        ScrollerLayoutInternal::switch_to_window(crossMonitorTarget, true);
        return;
    }

    const auto after = s->get_active_window();
    spdlog::info("move_focus: workspace={} direction={} after={} result={}",
                 workspace,
                 ScrollerLayoutInternal::direction_name(direction),
                 static_cast<const void*>(after ? after.get() : nullptr),
                 focus_move_result_name(moveResult));
    if (moveResult == FocusMoveResult::NoOp)
        return;

    ScrollerLayoutInternal::switch_to_window(s->get_active_window(), true);
}

void ScrollerLayout::move_window(int workspace, Direction direction) {
    auto s = getRowForWorkspace(workspace);
    if (s == nullptr)
        return;

    s->move_active_column(direction);
    ScrollerLayoutInternal::switch_to_window(s->get_active_window());
}

void ScrollerLayout::align_window(int workspace, Direction direction) {
    auto s = getRowForWorkspace(workspace);
    if (s == nullptr)
        return;

    s->align_column(direction);
}

void ScrollerLayout::admit_window_left(int workspace) {
    auto s = getRowForWorkspace(workspace);
    if (s == nullptr)
        return;

    s->admit_window_left();
}

void ScrollerLayout::expel_window_right(int workspace) {
    auto s = getRowForWorkspace(workspace);
    if (s == nullptr)
        return;

    s->expel_window_right();
}

void ScrollerLayout::set_mode(int workspace, Mode mode) {
    auto s = getRowForWorkspace(workspace);
    if (s == nullptr)
        return;

    s->set_mode(mode);
}

void ScrollerLayout::fit_size(int workspace, FitSize fitsize) {
    auto s = getRowForWorkspace(workspace);
    if (s == nullptr)
        return;

    s->fit_size(fitsize);
}

void ScrollerLayout::toggle_overview(int workspace) {
    auto s = getRowForWorkspace(workspace);
    if (s == nullptr)
        return;

    s->toggle_overview();
}

void ScrollerLayout::toggle_fullscreen(int workspace) {
    auto s = getRowForWorkspace(workspace);
    if (s == nullptr)
        return;

    s->toggle_fullscreen_active_window();
}
