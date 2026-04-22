/**
 * @file model.cpp
 * @brief Overview model construction from read-only canvas snapshots.
 *
 * The model is the central queryable snapshot used by overview session code:
 * it groups targets by monitor/workspace for layout and rendering, and also
 * flattens those targets into a graph-friendly list for navigation logic.
 */
#include "model.h"

#include <algorithm>

#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/desktop/Workspace.hpp>

#include "../layout/canvas/internal.h"
#include "model_layout.h"
#include "scene_layout.h"

namespace Overview {
namespace {

// A workspace may be logically assigned to one monitor but visibly rendered on
// another one, especially for special workspaces. Prefer the currently visible
// monitor whenever possible so overview reflects what the user actually sees.
PHLMONITOR monitor_for_workspace(PHLWORKSPACE workspace) {
    if (!workspace)
        return nullptr;

    if (const auto monitor = CanvasLayoutInternal::visible_monitor_for_workspace(workspace))
        return monitor;

    return g_pCompositor->getMonitorFromID(workspace->monitorID());
}

// Overview intentionally ignores floating/unmapped windows so navigation and
// rendering only deal with tiled content produced by the canvas layout.
bool is_tiled_overview_window(PHLWINDOW window) {
    return window && window->m_isMapped && !window->m_isFloating;
}

MonitorRegion make_monitor_region(PHLMONITOR monitor) {
    // Overview regions use the same canvas bounds as the layout itself so the
    // rendered overview tiles line up with normal workspace geometry.
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
    // Prefer the monitor id captured in the snapshot, then fall back to a fresh
    // runtime lookup in case the workspace moved between snapshotting and model
    // rebuild.
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

    // Snapshot windows are already laid out by the canvas layer. Model building
    // only filters and rewraps them into overview targets.
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

void project_workspace_targets(PHLMONITOR monitor, WorkspaceNode& workspace) {
    if (!monitor || workspace.targets.empty())
        return;

    const auto contentBox = buildWorkspaceContentBox(workspace.box);
    std::vector<size_t> windowIndexes;
    std::vector<ScrollerCore::Box> sourceBoxes;
    windowIndexes.reserve(workspace.targets.size());
    sourceBoxes.reserve(workspace.targets.size());

    // The overview model keeps one geometry per target. Rewriting those boxes
    // into final preview positions makes navigation, synthetic target creation,
    // and rendering all speak the same coordinate system.
    for (size_t index = 0; index < workspace.targets.size(); ++index) {
        const auto& target = workspace.targets[index];
        if (target.type != TargetType::Window || !target.window)
            continue;

        windowIndexes.push_back(index);
        sourceBoxes.push_back(target.box);
    }

    if (windowIndexes.empty()) {
        const auto emptyPreviewBox = buildEmptyWorkspacePreviewBox(contentBox);
        for (auto& target : workspace.targets)
            target.box = emptyPreviewBox;
        return;
    }

    const auto projectedBoxes = projectGlobalBoxesToContent(sourceBoxes,
                                                            contentBox,
                                                            monitor->m_position.x,
                                                            monitor->m_position.y);
    for (size_t index = 0; index < windowIndexes.size() && index < projectedBoxes.size(); ++index)
        workspace.targets[windowIndexes[index]].box = projectedBoxes[index];
}

} // namespace

void Model::clear() {
    // `clear()` resets both structured monitor/workspace data and all derived
    // navigation helpers so the object returns to a fully empty state.
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
    // Synthetic selections represent temporary "new empty workspace" targets
    // that do not live inside the persistent monitor/workspace tree.
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
    // TargetRef stores indexes back into the structured monitor/workspace tree.
    // Synthetic targets bypass that tree and point to a dedicated side slot.
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
    // Lookups are served from the flattened target graph because callers usually
    // care about "all selectable things", not the nested storage shape.
    for (const auto& node : targetGraph_) {
        const auto* target = resolve(node.ref);
        if (target && target->window == window)
            return node.ref;
    }

    return std::nullopt;
}

std::optional<TargetRef> Model::findByWorkspace(WORKSPACEID workspaceId) const {
    // Workspace-level lookup returns the first selectable target belonging to
    // that workspace, which may be a real window or the empty-workspace target.
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

    // The nested monitor/workspace tree is convenient for rendering and layout.
    // The target graph is the complementary flat view used by navigation logic.
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

    // Keep the temporary synthetic target navigable by appending it to the same
    // flat graph rather than teaching every caller a second lookup path.
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
    // Rebuild is a full snapshot refresh:
    // 1. enumerate monitors into empty regions
    // 2. ask every canvas-backed workspace for a read-only snapshot
    // 3. attach each workspace to the correct monitor region
    // 4. lay out workspace tiles inside every region
    // 5. project targets into final preview geometry
    // 6. flatten the result into the navigation graph
    monitors_.clear();
    selectionRef_.reset();
    syntheticSelection_.reset();
    targetGraph_.clear();

    for (const auto& monitor : g_pCompositor->m_monitors) {
        if (!monitor)
            continue;

        monitors_.push_back(make_monitor_region(monitor));
    }

    // Only workspaces backed by a live CanvasLayout participate in overview.
    // That keeps overview aligned with the plugin's own layout state.
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

    // Grid layout fills in workspace boxes and injects empty targets for any
    // workspace that has no tiled windows. Afterwards each target box is
    // rewritten into the exact preview geometry that overview will render.
    for (auto& region : monitors_) {
        layoutWorkspaceGrid(region);
        for (auto& workspace : region.workspaces)
            project_workspace_targets(region.monitor, workspace);
    }

    rebuildTargetGraph();
}

} // namespace Overview
