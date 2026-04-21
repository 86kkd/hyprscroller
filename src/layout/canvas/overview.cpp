/**
 * @file overview.cpp
 * @brief Read-only overview snapshot helpers for `CanvasLayout`.
 */
#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/helpers/Monitor.hpp>

#include "../lane/lane.h"
#include "layout.h"

void CanvasLayout::prepareForOverviewSnapshot() {
    // Overview captures whatever geometry the live layout currently exposes, so
    // the workspace runtime and lane geometry must be fresh before snapshotting.
    ensureWorkspaceRuntime();
    prepareForActionContext();

    const auto workspace = getCanvasWorkspace();
    if (!workspace)
        return;

    auto monitor = getVisibleCanvasMonitor(g_pCompositor->getMonitorFromID(workspace->monitorID()));
    if (!monitor)
        return;

    // Snapshot preparation is read-only at the overview layer, but it still
    // asks the canvas to relayout first so window boxes reflect the latest
    // monitor state, gaps, and fullscreen rules.
    relayoutCanvas(monitor, !workspace->m_isSpecialWorkspace);
}

CanvasOverviewSnapshot CanvasLayout::buildOverviewSnapshot() const {
    CanvasOverviewSnapshot snapshot;

    const auto workspace = getCanvasWorkspace();
    if (!workspace)
        return snapshot;

    snapshot.workspaceId = workspace->m_id;

    const auto monitor = getVisibleCanvasMonitor(g_pCompositor->getMonitorFromID(workspace->monitorID()));
    snapshot.monitorId = monitor ? monitor->m_id : workspace->monitorID();

    // The snapshot is intentionally lightweight: it carries only monitor/workspace
    // ids plus already-laid-out window boxes so overview model code stays pure.
    for (auto laneNode = lanes.first(); laneNode != nullptr; laneNode = laneNode->next()) {
        auto* lane = laneNode->data();
        if (!lane)
            continue;

        lane->for_each_window([&](PHLWINDOW window) {
            if (!window)
                return;

            snapshot.windows.push_back({
                .window = window,
                .box = {window->m_position.x, window->m_position.y, window->m_size.x, window->m_size.y},
            });
        });
    }

    return snapshot;
}
