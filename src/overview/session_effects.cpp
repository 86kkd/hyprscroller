/**
 * @file session_effects.cpp
 * @brief Runtime-facing helpers that apply overview session decisions to Hyprland state.
 *
 * `logic.cpp` decides what the overview session wants to do. This file is the
 * imperative layer that turns those decisions into workspace switches, monitor
 * focus changes, and window selection in the live compositor state.
 */
#include "session_effects.h"

#include <algorithm>

#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/desktop/Workspace.hpp>
#include <spdlog/spdlog.h>

#include "../layout/canvas/internal.h"

namespace Overview::SessionEffects {
namespace {

int resolved_monitor_id(PHLWORKSPACE workspace, PHLWINDOW window, int fallbackMonitorId) {
    // Prefer the monitor that currently owns the concrete window, because that
    // is the most specific target when a workspace spans monitor transitions.
    if (window) {
        if (const auto monitor = g_pCompositor->getMonitorFromID(window->monitorID()))
            return monitor->m_id;
    }

    // Otherwise fall back to the workspace's visible monitor if it has one, and
    // finally to the workspace's stored monitor id / caller-provided fallback.
    if (workspace) {
        if (const auto monitor = CanvasLayoutInternal::visible_monitor_for_workspace(workspace))
            return monitor->m_id;

        if (const auto workspaceMonitor = g_pCompositor->getMonitorFromID(workspace->monitorID()))
            return workspaceMonitor->m_id;
    }

    return fallbackMonitorId;
}

bool focus_workspace_target(PHLWORKSPACE workspace, WORKSPACEID workspaceId, int monitorId, const char* context) {
    const auto monitor = g_pCompositor->getMonitorFromID(monitorId);
    if (!monitor)
        return false;

    return CanvasLayoutInternal::focus_monitor_workspace(monitor, workspace, workspaceId, context);
}

void sync_canvas_target_window(PHLWORKSPACE workspace, PHLWINDOW window, int monitorId) {
    if (!workspace || !window)
        return;

    auto* layout = CanvasLayoutInternal::get_canvas_for_workspace(workspace->m_id);
    if (!layout)
        return;

    // Overview selection is purely logical while the overlay is open. Before we
    // hand real focus back to Hyprland, mirror that selection into the target
    // canvas so its active lane/window and paged geometry match the window that
    // is about to be focused.
    layout->onWindowFocusChange(window);
    layout->recalculateMonitor(monitorId);
}

} // namespace

OriginState captureOrigin() {
    auto originWorkspace = CanvasLayoutInternal::get_workspace_id();
    auto originWindow = PHLWINDOW{};
    auto originMonitor = MONITOR_INVALID;

    // Capture enough information to restore the user's pre-overview context even
    // if a window closes or the workspace later moves to another monitor.
    if (const auto workspace = g_pCompositor->getWorkspaceByID(originWorkspace)) {
        originWindow = workspace->getLastFocusedWindow();
        originMonitor = resolved_monitor_id(workspace, originWindow, workspace->monitorID());
    }

    if (originMonitor == MONITOR_INVALID) {
        if (const auto monitor = g_pCompositor->getMonitorFromCursor())
            originMonitor = monitor->m_id;
    }

    return {
        .monitorId = static_cast<int>(originMonitor),
        .workspaceId = originWorkspace,
        .window = originWindow,
    };
}

void prepareSnapshots() {
    // Each canvas captures its own render snapshot. Overview only needs to ask
    // every live canvas to freeze the current workspace state before animating.
    for (const auto& workspaceRef : g_pCompositor->getWorkspaces()) {
        const auto workspace = workspaceRef.lock();
        if (!workspace)
            continue;

        auto* layout = CanvasLayoutInternal::get_canvas_for_workspace(workspace->m_id);
        if (!layout)
            continue;

        layout->prepareForOverviewSnapshot();
    }
}

WORKSPACEID nextWorkspaceId() {
    WORKSPACEID maxWorkspaceId = 0;

    // Synthetic overview tiles for "new workspace" targets just need an unused
    // integer id beyond the current maximum.
    for (const auto& workspaceRef : g_pCompositor->getWorkspaces()) {
        const auto workspace = workspaceRef.lock();
        if (!workspace)
            continue;

        maxWorkspaceId = std::max(maxWorkspaceId, workspace->m_id);
    }

    return maxWorkspaceId + 1;
}

bool focus_window_target(PHLWORKSPACE workspace, PHLWINDOW window, int monitorId, bool warpCursor, const char* context) {
    if (!workspace || !window)
        return false;

    if (!focus_workspace_target(workspace, workspace->m_id, monitorId, context))
        return false;

    sync_canvas_target_window(workspace, window, monitorId);
    CanvasLayoutInternal::switch_to_window(window, warpCursor);
    return g_pCompositor && g_pCompositor->isWindowActive(window);
}

bool acceptTarget(const Target& selection) {
    const auto workspace = g_pCompositor->getWorkspaceByID(selection.workspaceId);
    const auto monitorId = resolved_monitor_id(workspace, selection.window, selection.monitorId);

    // Empty-workspace selections stop after focusing/creating the workspace.
    // There is no concrete window to activate afterward.
    if (selection.type == TargetType::EmptyWorkspace) {
        const auto focused = focus_workspace_target(workspace, selection.workspaceId, monitorId, "overview_accept_empty");
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

    // Window targets require both a resolvable workspace and a live window.
    // If either disappeared during overview we log and abort instead of trying
    // to focus dangling compositor objects.
    if (!workspace || !selection.window) {
        spdlog::warn("overview_accept_window: invalid target workspace={} window={}",
                     selection.workspaceId,
                     static_cast<const void*>(selection.window ? selection.window.get() : nullptr));
        return false;
    }

    // The happy path is a two-step restore:
    // 1. move focus to the correct workspace/monitor
    // 2. then focus the exact target window inside that workspace
    if (!focus_window_target(workspace, selection.window, monitorId, true, "overview_accept_window")) {
        spdlog::warn("overview_accept_window: focus failed workspace={} window={} special={}",
                     selection.workspaceId,
                     static_cast<const void*>(selection.window.get()),
                     workspace->m_isSpecialWorkspace);
        return false;
    }
    spdlog::info("overview_accept_window: workspace={} window={} special={}",
                 selection.workspaceId,
                 static_cast<const void*>(selection.window.get()),
                 workspace->m_isSpecialWorkspace);
    return true;
}

bool restoreOrigin(const OriginState& origin) {
    const auto workspace = g_pCompositor->getWorkspaceByID(origin.workspaceId);
    const auto monitorId = resolved_monitor_id(workspace, origin.window, origin.monitorId);

    // Best case: the original window still exists and is mapped, so we can
    // restore both workspace and exact window focus.
    if (origin.window && origin.window->m_isMapped && workspace) {
        if (!focus_window_target(workspace, origin.window, monitorId, false, "overview_restore_origin_window")) {
            spdlog::warn("overview_restore_origin_window: focus failed workspace={} monitor={} window={}",
                         workspace->m_id,
                         monitorId,
                         static_cast<const void*>(origin.window.get()));
            return false;
        }
        spdlog::info("overview_restore_origin_window: workspace={} monitor={} window={}",
                     workspace->m_id,
                     monitorId,
                     static_cast<const void*>(origin.window.get()));
        return true;
    }

    // Fallback: if the window disappeared, at least return to the original
    // workspace so the user lands in the right context.
    if (!workspace)
        return false;

    if (!focus_workspace_target(workspace, workspace->m_id, monitorId, "overview_restore_origin_workspace")) {
        spdlog::warn("overview_restore_origin_workspace: failed workspace={} monitor={} special={}",
                     workspace->m_id,
                     monitorId,
                     workspace->m_isSpecialWorkspace);
        return false;
    }
    spdlog::info("overview_restore_origin_workspace: workspace={} monitor={} special={}",
                 workspace->m_id,
                 monitorId,
                 workspace->m_isSpecialWorkspace);
    return true;
}

} // namespace Overview::SessionEffects
