/**
 * @file session_effects.cpp
 * @brief Runtime-facing helpers that apply overview session decisions to Hyprland state.
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
    if (window) {
        if (const auto monitor = g_pCompositor->getMonitorFromID(window->monitorID()))
            return monitor->m_id;
    }

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

    if (selection.type == TargetType::EmptyWorkspace) {
        focus_workspace_target(workspace, selection.workspaceId, monitorId, "overview_accept_empty");
        spdlog::info("overview_accept_empty: workspace={} monitor={} synthetic={}",
                     selection.workspaceId,
                     monitorId,
                     selection.synthetic);
        return;
    }

    if (!workspace || !selection.window) {
        spdlog::warn("overview_accept_window: invalid target workspace={} window={}",
                     selection.workspaceId,
                     static_cast<const void*>(selection.window ? selection.window.get() : nullptr));
        return;
    }

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

    if (origin.window && origin.window->m_isMapped && workspace) {
        focus_workspace_target(workspace, workspace->m_id, monitorId, "overview_restore_origin_window");
        CanvasLayoutInternal::switch_to_window(origin.window, false);
        spdlog::info("overview_restore_origin_window: workspace={} monitor={} window={}",
                     workspace->m_id,
                     monitorId,
                     static_cast<const void*>(origin.window.get()));
        return;
    }

    if (!workspace)
        return;

    focus_workspace_target(workspace, workspace->m_id, monitorId, "overview_restore_origin_workspace");
    spdlog::info("overview_restore_origin_workspace: workspace={} monitor={} special={}",
                 workspace->m_id,
                 monitorId,
                 workspace->m_isSpecialWorkspace);
}

} // namespace Overview::SessionEffects
