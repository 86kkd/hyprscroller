/**
 * @file model.cpp
 * @brief Overview model construction from read-only canvas snapshots.
 */
#include "model.h"

#include <algorithm>

#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/desktop/Workspace.hpp>

#include "../layout/canvas/internal.h"
#include "model_layout.h"

namespace Overview {
namespace {

PHLMONITOR monitor_for_workspace(PHLWORKSPACE workspace) {
    if (!workspace)
        return nullptr;

    if (const auto monitor = CanvasLayoutInternal::visible_monitor_for_workspace(workspace))
        return monitor;

    return g_pCompositor->getMonitorFromID(workspace->monitorID());
}

bool is_tiled_overview_window(PHLWINDOW window) {
    return window && window->m_isMapped && !window->m_isFloating;
}

MonitorRegion make_monitor_region(PHLMONITOR monitor) {
    const auto bounds = CanvasLayoutInternal::compute_canvas_bounds(monitor);
    return {
        .monitorId = static_cast<int>(monitor->m_id),
        .monitor = monitor,
        .box = bounds.max,
        .workspaces = {},
    };
}

MonitorRegion* find_region_by_monitor_id(std::vector<MonitorRegion>& monitors, int monitorId) {
    if (monitorId == MONITOR_INVALID)
        return nullptr;

    const auto regionIt = std::find_if(monitors.begin(), monitors.end(), [&](const MonitorRegion& region) {
        return region.monitorId == monitorId;
    });
    return regionIt == monitors.end() ? nullptr : &*regionIt;
}

MonitorRegion* resolve_workspace_region(std::vector<MonitorRegion>& monitors, int snapshotMonitorId, PHLWORKSPACE workspace) {
    if (auto* region = find_region_by_monitor_id(monitors, snapshotMonitorId))
        return region;

    const auto monitor = monitor_for_workspace(workspace);
    if (!monitor)
        return nullptr;

    return find_region_by_monitor_id(monitors, monitor->m_id);
}

WorkspaceNode build_workspace_node(const CanvasOverviewSnapshot& snapshot, int monitorId) {
    WorkspaceNode node;
    node.workspaceId = snapshot.workspaceId;
    node.monitorId = monitorId;

    for (const auto& snapshotWindow : snapshot.windows) {
        if (!is_tiled_overview_window(snapshotWindow.window))
            continue;

        node.targets.push_back({
            .type = TargetType::Window,
            .workspaceId = snapshot.workspaceId,
            .monitorId = monitorId,
            .window = snapshotWindow.window,
            .box = snapshotWindow.box,
            .synthetic = false,
        });
    }

    return node;
}

} // namespace

void Model::clear() {
    origin_ = {};
    monitors_.clear();
    targetGraph_.clear();
    selectionRef_.reset();
    syntheticSelection_.reset();
}

void Model::setOrigin(int monitorId, WORKSPACEID workspaceId, PHLWINDOW window) {
    origin_ = {
        .monitorId = monitorId,
        .workspaceId = workspaceId,
        .window = window,
    };
}

const OriginState& Model::origin() const {
    return origin_;
}

const std::vector<MonitorRegion>& Model::monitors() const {
    return monitors_;
}

const std::vector<TargetGraphNode>& Model::targetGraph() const {
    return targetGraph_;
}

const std::optional<TargetRef>& Model::selectionRef() const {
    return selectionRef_;
}

const Target* Model::selection() const {
    if (!selectionRef_)
        return nullptr;

    return resolve(*selectionRef_);
}

void Model::setSelection(const TargetRef& ref) {
    selectionRef_ = ref;
}

void Model::clearSelection() {
    selectionRef_.reset();
}

void Model::setSyntheticSelection(Target target) {
    syntheticSelection_ = std::move(target);
    selectionRef_ = TargetRef{.synthetic = true};
    rebuildTargetGraph();
}

void Model::clearSyntheticSelection() {
    syntheticSelection_.reset();
    if (selectionRef_ && selectionRef_->synthetic)
        selectionRef_.reset();
    rebuildTargetGraph();
}

const std::optional<Target>& Model::syntheticSelection() const {
    return syntheticSelection_;
}

const MonitorRegion* Model::regionForMonitor(int monitorId) const {
    for (const auto& region : monitors_) {
        if (region.monitorId == monitorId)
            return &region;
    }

    return nullptr;
}

const Target* Model::resolve(const TargetRef& ref) const {
    if (ref.synthetic)
        return syntheticSelection_ ? &*syntheticSelection_ : nullptr;

    if (ref.monitorIndex >= monitors_.size())
        return nullptr;

    const auto& monitor = monitors_[ref.monitorIndex];
    if (ref.workspaceIndex >= monitor.workspaces.size())
        return nullptr;

    const auto& workspace = monitor.workspaces[ref.workspaceIndex];
    if (ref.targetIndex >= workspace.targets.size())
        return nullptr;

    return &workspace.targets[ref.targetIndex];
}

std::optional<TargetRef> Model::findByWindow(PHLWINDOW window) const {
    for (const auto& node : targetGraph_) {
        const auto* target = resolve(node.ref);
        if (target && target->window == window)
            return node.ref;
    }

    return std::nullopt;
}

std::optional<TargetRef> Model::findByWorkspace(WORKSPACEID workspaceId) const {
    for (const auto& node : targetGraph_) {
        const auto* target = resolve(node.ref);
        if (target && target->workspaceId == workspaceId)
            return node.ref;
    }

    return std::nullopt;
}

std::optional<TargetRef> Model::firstTarget() const {
    if (targetGraph_.empty())
        return std::nullopt;

    return targetGraph_.front().ref;
}

void Model::rebuildTargetGraph() {
    targetGraph_.clear();

    for (std::size_t monitorIndex = 0; monitorIndex < monitors_.size(); ++monitorIndex) {
        const auto& monitor = monitors_[monitorIndex];
        for (std::size_t workspaceIndex = 0; workspaceIndex < monitor.workspaces.size(); ++workspaceIndex) {
            const auto& workspace = monitor.workspaces[workspaceIndex];
            for (std::size_t targetIndex = 0; targetIndex < workspace.targets.size(); ++targetIndex) {
                const auto& target = workspace.targets[targetIndex];
                targetGraph_.push_back({
                    .ref = TargetRef{
                        .synthetic = false,
                        .monitorIndex = monitorIndex,
                        .workspaceIndex = workspaceIndex,
                        .targetIndex = targetIndex,
                    },
                    .monitorId = target.monitorId,
                    .workspaceId = target.workspaceId,
                    .box = target.box,
                });
            }
        }
    }

    if (syntheticSelection_) {
        targetGraph_.push_back({
            .ref = TargetRef{.synthetic = true},
            .monitorId = syntheticSelection_->monitorId,
            .workspaceId = syntheticSelection_->workspaceId,
            .box = syntheticSelection_->box,
        });
    }
}

void Model::rebuild() {
    monitors_.clear();
    selectionRef_.reset();
    syntheticSelection_.reset();
    targetGraph_.clear();

    for (const auto& monitor : g_pCompositor->m_monitors) {
        if (!monitor)
            continue;

        monitors_.push_back(make_monitor_region(monitor));
    }

    for (const auto& workspaceRef : g_pCompositor->getWorkspaces()) {
        const auto workspace = workspaceRef.lock();
        if (!workspace)
            continue;

        auto* layout = CanvasLayoutInternal::get_canvas_for_workspace(workspace->m_id);
        if (!layout)
            continue;

        const auto snapshot = layout->buildOverviewSnapshot();
        auto* region = resolve_workspace_region(monitors_, snapshot.monitorId, workspace);
        if (!region)
            continue;

        region->workspaces.push_back(build_workspace_node(snapshot, region->monitorId));
    }

    for (auto& region : monitors_)
        layoutWorkspaceGrid(region);

    rebuildTargetGraph();
}

} // namespace Overview
