/**
 * @file session_effects_logic.cpp
 * @brief Runtime-agnostic overview accept/restore orchestration.
 */
#include "session_effects_runtime.h"

#include <algorithm>

#include <spdlog/spdlog.h>

namespace Overview::SessionEffects {
namespace {

constexpr MONITORID INVALID_MONITOR_ID = static_cast<MONITORID>(-1);

MONITORID resolved_monitor_id(const Runtime& runtime, PHLWORKSPACE workspace, PHLWINDOW window, MONITORID fallbackMonitorId) {
    if (window) {
        if (const auto monitor = runtime.getMonitorFromID(runtime.windowMonitorId(window)))
            return runtime.monitorId(monitor);
    }

    if (workspace) {
        if (const auto monitor = runtime.visibleMonitorForWorkspace(workspace))
            return runtime.monitorId(monitor);

        if (const auto workspaceMonitor = runtime.getMonitorFromID(runtime.workspaceMonitorId(workspace)))
            return runtime.monitorId(workspaceMonitor);
    }

    return fallbackMonitorId;
}

bool focus_workspace_target(const Runtime& runtime, PHLWORKSPACE workspace, WORKSPACEID workspaceId, MONITORID monitorId, const char* context) {
    const auto monitor = runtime.getMonitorFromID(monitorId);
    if (!monitor)
        return false;

    return runtime.focusMonitorWorkspace(monitor, workspace, workspaceId, context);
}

bool focus_window_target(const Runtime& runtime, PHLWORKSPACE workspace, PHLWINDOW window, MONITORID monitorId, bool warpCursor, const char* context) {
    if (!workspace || !window)
        return false;

    if (!focus_workspace_target(runtime, workspace, runtime.workspaceId(workspace), monitorId, context))
        return false;

    runtime.syncCanvasTargetWindow(workspace, window, monitorId);
    return runtime.switchToWindow(window, warpCursor);
}

} // namespace

OriginState captureOrigin(const Runtime& runtime) {
    auto originWorkspace = runtime.currentWorkspaceId();
    auto originWindow = PHLWINDOW{};
    auto originMonitor = INVALID_MONITOR_ID;

    if (const auto workspace = runtime.getWorkspaceByID(originWorkspace)) {
        originWindow = runtime.workspaceLastFocusedWindow(workspace);
        originMonitor = resolved_monitor_id(runtime, workspace, originWindow, runtime.workspaceMonitorId(workspace));
    }

    if (originMonitor == INVALID_MONITOR_ID) {
        if (const auto monitor = runtime.getMonitorFromCursor())
            originMonitor = runtime.monitorId(monitor);
    }

    return {
        .monitorId = static_cast<int>(originMonitor),
        .workspaceId = originWorkspace,
        .window = originWindow,
    };
}

void prepareSnapshots(const Runtime& runtime) {
    for (const auto& workspace : runtime.getWorkspaces()) {
        if (!workspace)
            continue;

        runtime.prepareWorkspaceSnapshot(workspace);
    }
}

WORKSPACEID nextWorkspaceId(const Runtime& runtime) {
    WORKSPACEID maxWorkspaceId = 0;

    for (const auto& workspace : runtime.getWorkspaces()) {
        if (!workspace)
            continue;

        maxWorkspaceId = std::max(maxWorkspaceId, runtime.workspaceId(workspace));
    }

    return maxWorkspaceId + 1;
}

bool acceptTarget(const Runtime& runtime, const Target& selection) {
    const auto workspace = runtime.getWorkspaceByID(selection.workspaceId);
    const auto monitorId = resolved_monitor_id(runtime, workspace, selection.window, selection.monitorId);

    if (selection.type == TargetType::EmptyWorkspace) {
        const auto focused = focus_workspace_target(runtime, workspace, selection.workspaceId, monitorId, "overview_accept_empty");
        if (!focused) {
            spdlog::warn("overview_accept_empty: failed workspace={} monitor={} synthetic={}",
                         selection.workspaceId,
                         monitorId,
                         selection.synthetic);
            return false;
        }
        spdlog::info("overview_accept_empty: workspace={} monitor={} synthetic={}",
                     selection.workspaceId,
                     monitorId,
                     selection.synthetic);
        return true;
    }

    if (!workspace || !selection.window) {
        spdlog::warn("overview_accept_window: invalid target workspace={} window={}",
                     selection.workspaceId,
                     static_cast<const void*>(selection.window ? selection.window.get() : nullptr));
        return false;
    }

    if (!focus_window_target(runtime, workspace, selection.window, monitorId, true, "overview_accept_window")) {
        spdlog::warn("overview_accept_window: focus failed workspace={} window={} special={}",
                     selection.workspaceId,
                     static_cast<const void*>(selection.window.get()),
                     runtime.isWorkspaceSpecial(workspace));
        return false;
    }
    spdlog::info("overview_accept_window: workspace={} window={} special={}",
                 selection.workspaceId,
                 static_cast<const void*>(selection.window.get()),
                 runtime.isWorkspaceSpecial(workspace));
    return true;
}

bool restoreOrigin(const Runtime& runtime, const OriginState& origin) {
    const auto workspace = runtime.getWorkspaceByID(origin.workspaceId);
    const auto monitorId = resolved_monitor_id(runtime, workspace, origin.window, origin.monitorId);

    if (origin.window && runtime.isWindowMapped(origin.window) && workspace) {
        if (!focus_window_target(runtime, workspace, origin.window, monitorId, false, "overview_restore_origin_window")) {
            spdlog::warn("overview_restore_origin_window: focus failed workspace={} monitor={} window={}",
                         runtime.workspaceId(workspace),
                         monitorId,
                         static_cast<const void*>(origin.window.get()));
            return false;
        }
        spdlog::info("overview_restore_origin_window: workspace={} monitor={} window={}",
                     runtime.workspaceId(workspace),
                     monitorId,
                     static_cast<const void*>(origin.window.get()));
        return true;
    }

    if (!workspace)
        return false;

    if (!focus_workspace_target(runtime, workspace, runtime.workspaceId(workspace), monitorId, "overview_restore_origin_workspace")) {
        spdlog::warn("overview_restore_origin_workspace: failed workspace={} monitor={} special={}",
                     runtime.workspaceId(workspace),
                     monitorId,
                     runtime.isWorkspaceSpecial(workspace));
        return false;
    }
    spdlog::info("overview_restore_origin_workspace: workspace={} monitor={} special={}",
                 runtime.workspaceId(workspace),
                 monitorId,
                 runtime.isWorkspaceSpecial(workspace));
    return true;
}

} // namespace Overview::SessionEffects
