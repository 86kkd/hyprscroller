#include <cstdio>
#include <string>

#include <spdlog/spdlog.h>

#include "dispatch_runtime.h"

namespace {

constexpr WORKSPACEID INVALID_WORKSPACE_ID = static_cast<WORKSPACEID>(-1);

std::string monitor_name(const CanvasLayoutInternal::DispatcherRuntime& runtime, PHLMONITOR monitor) {
    const auto name = runtime.monitorName(monitor);
    return name.empty() ? "unknown" : name;
}

bool is_cursor_monitor(const CanvasLayoutInternal::DispatcherRuntime& runtime, PHLMONITOR monitor) {
    if (!monitor)
        return false;

    return runtime.getMonitorFromCursor() == monitor;
}

} // namespace

namespace CanvasLayoutInternal {

bool focus_monitor_workspace(const DispatcherRuntime& runtime,
                             PHLMONITOR monitor,
                             PHLWORKSPACE workspace,
                             WORKSPACEID fallback_workspace_id,
                             const char* context) {
    const auto *ctx = context ? context : "focus_monitor_workspace";
    if (!monitor)
        return false;

    const auto targetMonitorName = runtime.monitorName(monitor);
    const auto targetWorkspaceId = workspace ? runtime.workspaceID(workspace) : fallback_workspace_id;
    if (targetMonitorName.empty()) {
        spdlog::warn("{}: monitor name missing for workspace focus workspace={}",
                     ctx,
                     targetWorkspaceId);
        return false;
    }

    const auto selector = runtime.workspaceSelector(workspace);
    const auto fallbackSelector = (!workspace && targetWorkspaceId != INVALID_WORKSPACE_ID)
                                    ? std::to_string(targetWorkspaceId)
                                    : std::string();
    const auto specialWorkspace = workspace && runtime.isWorkspaceSpecial(workspace);
    const auto priorMonitor = runtime.getMonitorFromCursor();
    const auto priorMonitorName = monitor_name(runtime, priorMonitor);

    spdlog::debug("{}: focusing monitor={} workspace={} prior_monitor={}",
                  ctx,
                  targetMonitorName,
                  targetWorkspaceId,
                  priorMonitorName);

    const auto switched = runtime.activateMonitorWorkspaceDirectly(monitor, workspace, fallback_workspace_id);
    if (switched) {
        spdlog::debug("{}: monitor {} switched to workspace {} via direct API",
                      ctx,
                      targetMonitorName,
                      targetWorkspaceId);
    }

    if (!is_cursor_monitor(runtime, monitor))
        (void)invoke_dispatcher(runtime, "focusmonitor", targetMonitorName, ctx);

    if (workspace && !specialWorkspace && !selector.empty())
        (void)invoke_dispatcher(runtime, "workspace", selector, ctx);
    else if (!workspace && !fallbackSelector.empty())
        (void)invoke_dispatcher(runtime, "workspace", fallbackSelector, ctx);

    if (!is_cursor_monitor(runtime, monitor)) {
        const auto retryContext = std::string(ctx).append("_retry");
        (void)invoke_dispatcher(runtime, "focusmonitor", targetMonitorName, retryContext.c_str());
        if (workspace && !specialWorkspace && !selector.empty())
            (void)invoke_dispatcher(runtime, "workspace", selector, retryContext.c_str());
        else if (!workspace && !fallbackSelector.empty())
            (void)invoke_dispatcher(runtime, "workspace", fallbackSelector, retryContext.c_str());
    }

    const auto focusedMonitor = runtime.getMonitorFromCursor();
    spdlog::debug("{}: monitor {} focus result prior={} post={} switched={} final_target_active={}",
                  ctx,
                  targetMonitorName,
                  priorMonitorName,
                  monitor_name(runtime, focusedMonitor),
                  switched,
                  is_cursor_monitor(runtime, monitor));

    const auto monitorFocused = focusedMonitor == monitor;
    const auto workspaceActive = runtime.isWorkspaceActiveOnMonitor(monitor, workspace, fallback_workspace_id);
    if (monitorFocused && workspaceActive)
        return true;

    if (workspace && !specialWorkspace && !selector.empty()) {
        spdlog::warn("{}: monitor {} still not focused after attempts workspace={} target={}",
                     ctx,
                     targetMonitorName,
                     targetWorkspaceId,
                     monitor_name(runtime, focusedMonitor));
        (void)invoke_dispatcher(runtime, "focusmonitor", targetMonitorName, "focus_monitor_workspace_verify");
        (void)invoke_dispatcher(runtime, "workspace", selector, "focus_monitor_workspace_verify");
    } else if (!workspace && !fallbackSelector.empty()) {
        spdlog::warn("{}: monitor {} still not focused after attempts fallback_workspace={} target={}",
                     ctx,
                     targetMonitorName,
                     targetWorkspaceId,
                     monitor_name(runtime, focusedMonitor));
        (void)invoke_dispatcher(runtime, "focusmonitor", targetMonitorName, "focus_monitor_workspace_verify");
        (void)invoke_dispatcher(runtime, "workspace", fallbackSelector, "focus_monitor_workspace_verify");
    }

    const auto finalFocusedMonitor = runtime.getMonitorFromCursor();
    return finalFocusedMonitor == monitor
        && runtime.isWorkspaceActiveOnMonitor(monitor, workspace, fallback_workspace_id);
}

void focus_window_monitor(const DispatcherRuntime& runtime, PHLWINDOW window) {
    if (!window)
        return;

    const auto targetMonitor = runtime.getMonitorFromID(runtime.windowMonitorID(window));
    const auto currentMonitor = runtime.getMonitorFromCursor();
    const auto targetMonitorName = runtime.monitorName(targetMonitor);
    if (!targetMonitor || !currentMonitor || targetMonitor == currentMonitor || targetMonitorName.empty())
        return;

    spdlog::debug("switch_to_window: focusing monitor={} before window={} workspace={}",
                  targetMonitorName,
                  static_cast<const void*>(window.get()),
                  runtime.windowWorkspaceID(window));
    (void)invoke_dispatcher(runtime, "focusmonitor", targetMonitorName, "focus_window_monitor");
}

bool switch_to_window(const DispatcherRuntime& runtime, PHLWINDOW window, bool warp_cursor)
{
    if (!window)
        return false;

    focus_window_monitor(runtime, window);

    if (!runtime.isWindowActive(window)) {
        spdlog::debug("switch_to_window: focusing window={} workspace={}",
                      static_cast<const void*>(window.get()),
                      runtime.windowWorkspaceID(window));
        char selector[64];
        std::snprintf(selector, sizeof(selector), "address:0x%lx",
                      reinterpret_cast<unsigned long>(window.get()));
        if (!invoke_dispatcher(runtime, "focuswindow", selector, "switch_to_window")) {
            spdlog::warn("switch_to_window: focuswindow dispatcher failed window={} workspace={}",
                         static_cast<const void*>(window.get()),
                         runtime.windowWorkspaceID(window));
        }
    }

    const auto active = runtime.isWindowActive(window);
    if (!active) {
        spdlog::warn("switch_to_window: window did not become active window={} workspace={} monitor={}",
                     static_cast<const void*>(window.get()),
                     runtime.windowWorkspaceID(window),
                     runtime.windowMonitorID(window));
        return false;
    }

    if (warp_cursor)
        runtime.warpCursorToWindow(window);

    return true;
}

} // namespace CanvasLayoutInternal
