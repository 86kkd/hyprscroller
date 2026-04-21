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
#include <string>

#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/desktop/Workspace.hpp>
#include <spdlog/spdlog.h>

#include "../core/workspace_selector.h"
#include "../layout/canvas/internal.h"
#include "logic.h"

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

bool execute_accept_plan(const std::vector<OverviewLogic::AcceptAction>& plan, PHLWORKSPACE workspace, const char* context) {
    for (const auto& step : plan) {
        // Plans are already ordered by OverviewLogic. This loop is intentionally
        // dumb: it only translates each abstract step into the matching Hyprland
        // dispatcher call and preserves the provided execution order.
        switch (step.type) {
            case OverviewLogic::AcceptActionType::FocusMonitor:
                if (const auto monitor = g_pCompositor->getMonitorFromID(step.monitorId)) {
                    if (!monitor->m_name.empty())
                        (void)CanvasLayoutInternal::invoke_dispatcher("focusmonitor", monitor->m_name, context);
                }
                break;
            case OverviewLogic::AcceptActionType::Workspace: {
                const auto selector = workspace ? ScrollerCore::workspace_selector(workspace) : std::to_string(step.workspaceId);
                (void)CanvasLayoutInternal::invoke_dispatcher("workspace", selector, context);
                break;
            }
            case OverviewLogic::AcceptActionType::ToggleSpecialWorkspace: {
                const auto selector = workspace ? ScrollerCore::workspace_selector(workspace) : std::to_string(step.workspaceId);
                (void)CanvasLayoutInternal::invoke_dispatcher("togglespecialworkspace", selector, context);
                break;
            }
        }
    }

    return true;
}

void focus_workspace_target(PHLWORKSPACE workspace, WORKSPACEID workspaceId, int monitorId, const char* context) {
    // Existing workspaces and synthetic empty-workspace targets need slightly
    // different action sequences, so OverviewLogic builds the right dispatcher
    // plan for us and this helper simply executes it.
    const auto acceptPlan = workspace
        ? OverviewLogic::buildWorkspaceAcceptPlan(monitorId, workspace->m_id, workspace->m_isSpecialWorkspace)
        : OverviewLogic::buildEmptyAcceptPlan(monitorId, workspaceId);
    execute_accept_plan(acceptPlan, workspace, context);
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

void acceptTarget(const Target& selection) {
    const auto workspace = g_pCompositor->getWorkspaceByID(selection.workspaceId);
    const auto monitorId = resolved_monitor_id(workspace, selection.window, selection.monitorId);

    // Empty-workspace selections stop after focusing/creating the workspace.
    // There is no concrete window to activate afterward.
    if (selection.type == TargetType::EmptyWorkspace) {
        focus_workspace_target(workspace, selection.workspaceId, monitorId, "overview_accept_empty");
        spdlog::info("overview_accept_empty: workspace={} monitor={} synthetic={}",
                     selection.workspaceId,
                     monitorId,
                     selection.synthetic);
        return;
    }

    // Window targets require both a resolvable workspace and a live window.
    // If either disappeared during overview we log and abort instead of trying
    // to focus dangling compositor objects.
    if (!workspace || !selection.window) {
        spdlog::warn("overview_accept_window: invalid target workspace={} window={}",
                     selection.workspaceId,
                     static_cast<const void*>(selection.window ? selection.window.get() : nullptr));
        return;
    }

    // The happy path is a two-step restore:
    // 1. move focus to the correct workspace/monitor
    // 2. then focus the exact target window inside that workspace
    focus_workspace_target(workspace, workspace->m_id, monitorId, "overview_accept_window");
    CanvasLayoutInternal::switch_to_window(selection.window, true);
    spdlog::info("overview_accept_window: workspace={} window={} special={}",
                 selection.workspaceId,
                 static_cast<const void*>(selection.window.get()),
                 workspace->m_isSpecialWorkspace);
}

void restoreOrigin(const OriginState& origin) {
    const auto workspace = g_pCompositor->getWorkspaceByID(origin.workspaceId);
    const auto monitorId = resolved_monitor_id(workspace, origin.window, origin.monitorId);

    // Best case: the original window still exists and is mapped, so we can
    // restore both workspace and exact window focus.
    if (origin.window && origin.window->m_isMapped && workspace) {
        focus_workspace_target(workspace, workspace->m_id, monitorId, "overview_restore_origin_window");
        CanvasLayoutInternal::switch_to_window(origin.window, false);
        spdlog::info("overview_restore_origin_window: workspace={} monitor={} window={}",
                     workspace->m_id,
                     monitorId,
                     static_cast<const void*>(origin.window.get()));
        return;
    }

    // Fallback: if the window disappeared, at least return to the original
    // workspace so the user lands in the right context.
    if (!workspace)
        return;

    focus_workspace_target(workspace, workspace->m_id, monitorId, "overview_restore_origin_workspace");
    spdlog::info("overview_restore_origin_workspace: workspace={} monitor={} special={}",
                 workspace->m_id,
                 monitorId,
                 workspace->m_isSpecialWorkspace);
}

} // namespace Overview::SessionEffects
