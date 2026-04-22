/**
 * @file dispatch.cpp
 * @brief Shared Hyprland dispatcher invocation helpers for canvas code.
 *
 * This file centralizes lookup, validation, logging and invocation of Hyprland
 * dispatchers so higher-level focus/window routing code no longer touches the
 * raw dispatcher map directly.
 */
#include <cstdio>
#include <string>

#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/helpers/Monitor.hpp>
#include <spdlog/spdlog.h>

#include "../../core/direction.h"
#include "../../core/workspace_selector.h"
#include "internal.h"

namespace {

// Default production runtime backed by Hyprland globals. Tests can replace it
// with a fake implementation through set_dispatcher_runtime_for_tests().
class HyprlandDispatcherRuntime final : public CanvasLayoutInternal::DispatcherRuntime {
public:
    bool hasDispatcherRegistry() const override {
        return static_cast<bool>(g_pKeybindManager);
    }

    bool hasDispatcher(const char *dispatcher) const override {
        if (!g_pKeybindManager)
            return false;

        return g_pKeybindManager->m_dispatchers.contains(dispatcher);
    }

    bool invokeDispatcher(const char *dispatcher, std::string_view arg) const override {
        if (!g_pKeybindManager)
            return false;

        const auto it = g_pKeybindManager->m_dispatchers.find(dispatcher);
        if (it == g_pKeybindManager->m_dispatchers.end())
            return false;

        it->second(std::string(arg));
        return true;
    }

    PHLMONITOR getMonitorFromID(int monitorId) const override {
        return g_pCompositor ? g_pCompositor->getMonitorFromID(monitorId) : nullptr;
    }

    PHLMONITOR getMonitorFromCursor() const override {
        return g_pCompositor ? g_pCompositor->getMonitorFromCursor() : nullptr;
    }

    bool isWindowActive(PHLWINDOW window) const override {
        return g_pCompositor && window && g_pCompositor->isWindowActive(window);
    }

    std::string monitorName(PHLMONITOR monitor) const override {
        return monitor ? monitor->m_name : std::string();
    }

    WORKSPACEID workspaceID(PHLWORKSPACE workspace) const override {
        return workspace ? workspace->m_id : WORKSPACE_INVALID;
    }

    bool isWorkspaceSpecial(PHLWORKSPACE workspace) const override {
        return workspace && workspace->m_isSpecialWorkspace;
    }

    std::string workspaceSelector(PHLWORKSPACE workspace) const override {
        return ScrollerCore::workspace_selector(workspace);
    }

    bool activateMonitorWorkspaceDirectly(PHLMONITOR monitor, PHLWORKSPACE workspace, WORKSPACEID fallbackWorkspaceId) const override {
        if (!monitor)
            return false;

        if (workspace) {
            if (workspace->m_isSpecialWorkspace)
                monitor->changeWorkspace(workspace->m_id, false, false, false);
            else
                monitor->changeWorkspace(workspace, false, false, false);
        } else if (fallbackWorkspaceId != WORKSPACE_INVALID) {
            monitor->changeWorkspace(fallbackWorkspaceId, false, false, false);
        }

        return isWorkspaceActiveOnMonitor(monitor, workspace, fallbackWorkspaceId);
    }

    bool isWorkspaceActiveOnMonitor(PHLMONITOR monitor, PHLWORKSPACE workspace, WORKSPACEID fallbackWorkspaceId) const override {
        if (!monitor)
            return false;

        if (workspace) {
            return workspace->m_isSpecialWorkspace
                ? monitor->activeSpecialWorkspaceID() == workspace->m_id
                : monitor->activeWorkspaceID() == workspace->m_id;
        }

        if (fallbackWorkspaceId != WORKSPACE_INVALID)
            return monitor->activeWorkspaceID() == fallbackWorkspaceId;

        return true;
    }

    MONITORID windowMonitorID(PHLWINDOW window) const override {
        return window ? window->monitorID() : MONITOR_INVALID;
    }

    WORKSPACEID windowWorkspaceID(PHLWINDOW window) const override {
        return window ? window->workspaceID() : WORKSPACE_INVALID;
    }

    void warpCursorToWindow(PHLWINDOW window) const override {
        if (window)
            window->warpCursor(true);
    }
};

CanvasLayoutInternal::DispatcherRuntime *g_dispatcherRuntimeOverride = nullptr;

CanvasLayoutInternal::DispatcherRuntime &dispatcher_runtime() {
    static HyprlandDispatcherRuntime runtime;
    return g_dispatcherRuntimeOverride ? *g_dispatcherRuntimeOverride : runtime;
}

} // namespace

namespace CanvasLayoutInternal {

void set_dispatcher_runtime_for_tests(DispatcherRuntime *runtime) {
    g_dispatcherRuntimeOverride = runtime;
}

bool can_invoke_dispatcher(const char* dispatcher, std::string_view arg, const char* context) {
    return CanvasLayoutInternal::can_invoke_dispatcher(dispatcher_runtime(), dispatcher, arg, context);
}

bool invoke_dispatcher(const char* dispatcher, std::string_view arg, const char* context) {
    return CanvasLayoutInternal::invoke_dispatcher(dispatcher_runtime(), dispatcher, arg, context);
}

// Shared wrapper around Hyprland builtin directional dispatchers.
void dispatch_directional_builtin(const char* dispatcher, Direction direction) {
    const auto arg = ScrollerCore::direction_dispatch_arg(direction);
    if (!arg) {
        spdlog::warn("dispatch_directional_builtin: unsupported direction={} dispatcher={}",
                     ScrollerCore::direction_name(direction),
                     dispatcher ? dispatcher : "(null)");
        return;
    }

    (void)invoke_dispatcher(dispatcher, arg, "dispatch_directional_builtin");
}

// Thin specialized wrapper for builtin movefocus.
void dispatch_builtin_movefocus(Direction direction) {
    dispatch_directional_builtin("movefocus", direction);
}

// Focus a monitor even when no concrete target window exists yet.
bool focus_monitor_workspace(PHLMONITOR monitor, PHLWORKSPACE workspace, WORKSPACEID fallback_workspace_id,
                             bool require_monitor_focus, const char* context) {
    return focus_monitor_workspace(dispatcher_runtime(), monitor, workspace, fallback_workspace_id, require_monitor_focus, context);
}

// Focus the monitor hosting a target window before focusing the window itself.
void focus_window_monitor(PHLWINDOW window) {
    focus_window_monitor(dispatcher_runtime(), window);
}

// Focus a target window and optionally warp the cursor to it.
bool switch_to_window(PHLWINDOW window, bool warp_cursor)
{
    return switch_to_window(dispatcher_runtime(), window, warp_cursor);
}

} // namespace CanvasLayoutInternal
