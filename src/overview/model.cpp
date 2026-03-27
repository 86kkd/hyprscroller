/**
 * @file model.cpp
 * @brief Overview model construction from read-only canvas snapshots.
 */
#include "model.h"

#include <algorithm>
#include <cmath>

#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/desktop/Workspace.hpp>

#include "../layout/canvas/internal.h"

namespace Overview {
namespace {

using ScrollerCore::Box;

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

Box inset_box(const Box& box, double ratio, double minimumInset = 18.0) {
    const auto insetX = std::min(std::max(minimumInset, box.w * ratio), std::max(0.0, box.w / 2.5));
    const auto insetY = std::min(std::max(minimumInset, box.h * ratio), std::max(0.0, box.h / 2.5));
    return {
        box.x + insetX,
        box.y + insetY,
        std::max(24.0, box.w - insetX * 2.0),
        std::max(24.0, box.h - insetY * 2.0),
    };
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

void finalize_workspace_targets(WorkspaceNode& node) {
    if (!node.targets.empty())
        return;

    node.targets.push_back(makeEmptyTarget(node.workspaceId, node.monitorId, node.box, false));
}

void layout_workspace_grid(MonitorRegion& region) {
    std::sort(region.workspaces.begin(), region.workspaces.end(), [](const WorkspaceNode& a, const WorkspaceNode& b) {
        return a.workspaceId < b.workspaceId;
    });

    const auto count = region.workspaces.size();
    if (count == 0)
        return;

    const auto aspect = region.box.h > 0.0 ? region.box.w / region.box.h : 1.0;
    const auto columns = std::max<std::size_t>(1, static_cast<std::size_t>(std::ceil(std::sqrt(static_cast<double>(count) * std::max(0.5, aspect)))));
    const auto rows = std::max<std::size_t>(1, static_cast<std::size_t>(std::ceil(static_cast<double>(count) / static_cast<double>(columns))));
    const auto horizontalGap = std::min(32.0, std::max(12.0, region.box.w * 0.02));
    const auto verticalGap = std::min(32.0, std::max(12.0, region.box.h * 0.03));
    const auto totalHorizontalGap = horizontalGap * static_cast<double>(columns - 1);
    const auto totalVerticalGap = verticalGap * static_cast<double>(rows - 1);
    const auto cellWidth = std::max(120.0, (region.box.w - totalHorizontalGap) / static_cast<double>(columns));
    const auto cellHeight = std::max(96.0, (region.box.h - totalVerticalGap) / static_cast<double>(rows));

    for (std::size_t index = 0; index < count; ++index) {
        auto& workspace = region.workspaces[index];
        const auto column = index % columns;
        const auto row = index / columns;
        workspace.box = {
            region.box.x + static_cast<double>(column) * (cellWidth + horizontalGap),
            region.box.y + static_cast<double>(row) * (cellHeight + verticalGap),
            cellWidth,
            cellHeight,
        };
        finalize_workspace_targets(workspace);
    }
}

} // namespace

Target makeEmptyTarget(WORKSPACEID workspaceId, int monitorId, const Box& workspaceBox, bool synthetic) {
    Target target;
    target.type = TargetType::EmptyWorkspace;
    target.workspaceId = workspaceId;
    target.monitorId = monitorId;
    target.window = nullptr;
    target.box = inset_box(workspaceBox, synthetic ? 0.16 : 0.20);
    target.synthetic = synthetic;
    return target;
}

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

WORKSPACEID Model::nextWorkspaceId() const {
    WORKSPACEID maxWorkspaceId = 0;

    for (const auto& workspaceRef : g_pCompositor->getWorkspaces()) {
        const auto workspace = workspaceRef.lock();
        if (!workspace)
            continue;

        maxWorkspaceId = std::max(maxWorkspaceId, workspace->m_id);
    }

    return maxWorkspaceId + 1;
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
        layout_workspace_grid(region);

    rebuildTargetGraph();
}

} // namespace Overview
