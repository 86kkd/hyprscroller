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
};

CanvasLayoutInternal::DispatcherRuntime *g_dispatcherRuntimeOverride = nullptr;

CanvasLayoutInternal::DispatcherRuntime &dispatcher_runtime() {
    static HyprlandDispatcherRuntime runtime;
    return g_dispatcherRuntimeOverride ? *g_dispatcherRuntimeOverride : runtime;
}

std::string monitor_name(PHLMONITOR monitor) {
    return monitor ? monitor->m_name : "unknown";
}

bool is_cursor_monitor(PHLMONITOR monitor) {
    if (!monitor || !g_pCompositor)
        return false;

    return g_pCompositor->getMonitorFromCursor() == monitor;
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
void focus_monitor_workspace(PHLMONITOR monitor, PHLWORKSPACE workspace, WORKSPACEID fallback_workspace_id, const char* context) {
    const auto *ctx = context ? context : "focus_monitor_workspace";
    if (!monitor)
        return;

    if (monitor->m_name.empty()) {
        spdlog::warn("{}: monitor name missing for workspace focus workspace={}",
                     ctx,
                     workspace ? workspace->m_id : fallback_workspace_id);
        return;
    }

    const auto targetWorkspaceId = workspace ? workspace->m_id : fallback_workspace_id;
    const auto selector = ScrollerCore::workspace_selector(workspace);
    const auto fallbackSelector = (!workspace && targetWorkspaceId != WORKSPACE_INVALID)
                                    ? std::to_string(targetWorkspaceId)
                                    : std::string();
    const auto priorMonitor = g_pCompositor ? g_pCompositor->getMonitorFromCursor() : nullptr;
    const auto priorMonitorName = monitor_name(priorMonitor);
    const auto targetMonitorName = monitor->m_name;

    spdlog::debug("{}: focusing monitor={} workspace={} prior_monitor={}",
                  ctx,
                  targetMonitorName,
                  targetWorkspaceId,
                  priorMonitorName);

    // Prefer direct monitor APIs first because they avoid dispatcher parsing and
    // can switch special workspaces explicitly. Dispatcher calls still follow as
    // verification/normalization because some compositor state only settles once
    // the public dispatch path runs.
    bool switched = false;
    if (workspace) {
        if (workspace->m_isSpecialWorkspace)
            monitor->changeWorkspace(workspace->m_id, false, false, false);
        else
            monitor->changeWorkspace(workspace, false, false, false);
        switched = workspace->m_isSpecialWorkspace
            ? monitor->activeSpecialWorkspaceID() == workspace->m_id
            : monitor->activeWorkspaceID() == workspace->m_id;
    } else if (targetWorkspaceId != WORKSPACE_INVALID) {
        monitor->changeWorkspace(targetWorkspaceId, false, false, false);
        switched = monitor->activeWorkspaceID() == targetWorkspaceId;
    }

    if (switched) {
        spdlog::debug("{}: monitor {} switched to workspace {} via direct API",
                      ctx,
                      targetMonitorName,
                      targetWorkspaceId);
    }

    // First public pass: make sure cursor focus and active workspace line up
    // with the direct API result.
    if (!is_cursor_monitor(monitor))
        (void)invoke_dispatcher("focusmonitor", monitor->m_name, ctx);

    if (workspace && !workspace->m_isSpecialWorkspace && !selector.empty())
        (void)invoke_dispatcher("workspace", selector, ctx);
    else if (!workspace && !fallbackSelector.empty())
        (void)invoke_dispatcher("workspace", fallbackSelector, ctx);

    // Retry once when cursor focus still did not move. This is defensive code
    // for compositor timing edges around monitor focus/workspace activation.
    if (!is_cursor_monitor(monitor)) {
        const auto retryContext = std::string(ctx).append("_retry");
        (void)invoke_dispatcher("focusmonitor", monitor->m_name, retryContext.c_str());
        if (workspace && !workspace->m_isSpecialWorkspace && !selector.empty())
            (void)invoke_dispatcher("workspace", selector, retryContext.c_str());
        else if (!workspace && !fallbackSelector.empty())
            (void)invoke_dispatcher("workspace", fallbackSelector, retryContext.c_str());
    }

    const auto focusedMonitor = g_pCompositor ? g_pCompositor->getMonitorFromCursor() : nullptr;
    spdlog::debug("{}: monitor {} focus result prior={} post={} switched={} final_target_active={}",
                  ctx,
                  targetMonitorName,
                  priorMonitorName,
                  monitor_name(focusedMonitor),
                  switched,
                  is_cursor_monitor(monitor));

    if (focusedMonitor == monitor)
        return;

    // Final verify pass with explicit log context so failures are easy to spot
    // in logs when cross-monitor focus regresses.
    if (workspace && !workspace->m_isSpecialWorkspace && !selector.empty()) {
        spdlog::warn("{}: monitor {} still not focused after attempts workspace={} target={}",
                     ctx,
                     targetMonitorName,
                     targetWorkspaceId,
                     monitor_name(focusedMonitor));
        (void)invoke_dispatcher("focusmonitor", monitor->m_name, "focus_monitor_workspace_verify");
        (void)invoke_dispatcher("workspace", selector, "focus_monitor_workspace_verify");
        return;
    }

    if (!workspace && !fallbackSelector.empty()) {
        spdlog::warn("{}: monitor {} still not focused after attempts fallback_workspace={} target={}",
                     ctx,
                     targetMonitorName,
                     targetWorkspaceId,
                     monitor_name(focusedMonitor));
        (void)invoke_dispatcher("focusmonitor", monitor->m_name, "focus_monitor_workspace_verify");
        (void)invoke_dispatcher("workspace", fallbackSelector, "focus_monitor_workspace_verify");
    }
}

// Focus the monitor hosting a target window before focusing the window itself.
void focus_window_monitor(PHLWINDOW window) {
    if (!window)
        return;

    const auto targetMonitor = dispatcher_runtime().getMonitorFromID(window->monitorID());
    const auto currentMonitor = dispatcher_runtime().getMonitorFromCursor();
    if (!targetMonitor || !currentMonitor || targetMonitor == currentMonitor || targetMonitor->m_name.empty())
        return;

    spdlog::debug("switch_to_window: focusing monitor={} before window={} workspace={}",
                  targetMonitor->m_name,
                  static_cast<const void*>(window.get()),
                  window->workspaceID());
    (void)invoke_dispatcher("focusmonitor", targetMonitor->m_name, "focus_window_monitor");
}

// Focus a target window and optionally warp the cursor to it.
void switch_to_window(PHLWINDOW window, bool warp_cursor)
{
    if (!window)
        return;

    // Window focus is split into two steps because Hyprland may reject focusing
    // a window on an unfocused monitor unless that monitor is focused first.
    focus_window_monitor(window);

    if (!dispatcher_runtime().isWindowActive(window)) {
        spdlog::debug("switch_to_window: focusing window={} workspace={}",
                      static_cast<const void*>(window.get()), window->workspaceID());
        char selector[64];
        std::snprintf(selector, sizeof(selector), "address:0x%lx",
                      reinterpret_cast<unsigned long>(window.get()));
        (void)invoke_dispatcher("focuswindow", selector, "switch_to_window");
    }

    if (warp_cursor)
        window->warpCursor(true);
}

} // namespace CanvasLayoutInternal
