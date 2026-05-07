/**
 * @file model.cpp
 * @brief Overview model construction from read-only canvas snapshots.
 *
 * The model is the central queryable snapshot used by overview session code:
 * it groups targets by monitor/workspace for layout and rendering, and also
 * flattens those targets into a graph-friendly list for navigation logic.
 */
#include "overview/model/model.h"

#include <algorithm>
#include <unordered_map>

#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/desktop/Workspace.hpp>
#include <hyprland/src/layout/algorithm/Algorithm.hpp>
#include <hyprland/src/layout/space/Space.hpp>

#include "layout/canvas/internal.h"
#include "layout/grid/layout.h"
#include "overview/navigation/logic.h"
#include "overview/model/layout.h"
#include "overview/scene/layout.h"

namespace Overview {
namespace {

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

const CanvasLayoutState::CanvasWorkspaceMember* find_member(const CanvasLayoutState::CanvasWorkspaceRecord& canvas, int monitorId) {
    const auto memberIt = std::find_if(canvas.members.begin(), canvas.members.end(), [&](const auto& member) {
        return member.monitorId == monitorId;
    });
    return memberIt == canvas.members.end() ? nullptr : &*memberIt;
}

bool is_synthetic_canvas(const std::vector<CanvasLayoutState::SyntheticCanvasWorkspace>& synthetics, int canvasId) {
    return std::any_of(synthetics.begin(), synthetics.end(), [&](const auto& synthetic) {
        return synthetic.canvas.canvasId == canvasId;
    });
}

const CanvasLayoutState::CanvasWorkspaceRecord* find_canvas_record(const std::vector<CanvasLayoutState::CanvasWorkspaceRecord>& canvases, int canvasId) {
    const auto canvasIt = std::find_if(canvases.begin(), canvases.end(), [&](const auto& canvas) {
        return canvas.canvasId == canvasId;
    });
    return canvasIt == canvases.end() ? nullptr : &*canvasIt;
}

ScrollerGrid::GridLayout* grid_layout_for_workspace(WORKSPACEID workspaceId) {
    const auto workspace = g_pCompositor->getWorkspaceByID(workspaceId);
    if (!workspace || !workspace->m_space)
        return nullptr;

    const auto algorithm = workspace->m_space->algorithm();
    if (!algorithm || !algorithm->tiledAlgo())
        return nullptr;

    return dynamic_cast<ScrollerGrid::GridLayout*>(algorithm->tiledAlgo().get());
}

WorkspaceNode build_workspace_node(const CanvasLayoutState::CanvasWorkspaceRecord& canvas,
                                   int monitorId,
                                   const CanvasLayoutState::CanvasWorkspaceMember* member,
                                   const CanvasOverviewSnapshot* snapshot,
                                   bool synthetic) {
    WorkspaceNode node;
    node.canvasId = canvas.canvasId;
    node.tileX = canvas.tileX;
    node.tileY = canvas.tileY;
    node.workspaceId = member ? member->workspaceId : INVALID_WORKSPACE_ID;
    node.monitorId = monitorId;
    node.specialWorkspace = member ? member->special : false;
    node.synthetic = synthetic;

    if (!snapshot)
        return node;

    // Snapshot windows are already laid out by the canvas layer. Model building
    // only filters and rewraps them into overview targets.
    for (const auto& snapshotWindow : snapshot->windows) {
        if (!is_tiled_overview_window(snapshotWindow.window))
            continue;

        node.targets.push_back({
            .type = TargetType::Window,
            .canvasId = canvas.canvasId,
            .workspaceId = node.workspaceId,
            .monitorId = monitorId,
            .specialWorkspace = node.specialWorkspace,
            .window = snapshotWindow.window,
            .box = snapshotWindow.box,
            .sourceBox = snapshotWindow.box,
            .synthetic = synthetic,
        });
    }

    return node;
}

void project_workspace_targets(PHLMONITOR monitor, WorkspaceNode& workspace) {
    if (!monitor || workspace.targets.empty())
        return;

    const auto contentBox = workspace.box;
    std::vector<size_t> windowIndexes;
    std::vector<ScrollerCore::Box> sourceBoxes;
    windowIndexes.reserve(workspace.targets.size());
    sourceBoxes.reserve(workspace.targets.size());

    // Keep the original logical window box alongside the projected preview box.
    // Overview rendering/animation needs both:
    // - `sourceBox` for "where the real layout says this window is"
    // - `box` for "where the preview card should end up"
    for (size_t index = 0; index < workspace.targets.size(); ++index) {
        const auto& target = workspace.targets[index];
        if (target.type != TargetType::Window || !target.window)
            continue;

        windowIndexes.push_back(index);
        sourceBoxes.push_back(target.sourceBox);
    }

    if (windowIndexes.empty()) {
        for (auto& target : workspace.targets)
            target.box = contentBox;
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
    canvasGraph_.clear();
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

const std::vector<CanvasGraphNode>& Model::canvasGraph() const {
    return canvasGraph_;
}

const std::optional<TargetRef>& Model::selectionRef() const {
    return selectionRef_;
}

const Target* Model::selection() const {
    if (!selectionRef_)
        return nullptr;

    return resolve(*selectionRef_);
}

std::optional<int> Model::selectionCanvasId() const {
    const auto* selected = selection();
    if (!selected)
        return std::nullopt;

    return selected->canvasId;
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
    if (!window)
        return std::nullopt;

    // Lookups are served from the flattened target graph because callers usually
    // care about "all selectable things", not the nested storage shape.
    for (const auto& node : targetGraph_) {
        const auto* target = resolve(node.ref);
        if (target && target->type == TargetType::Window && target->window == window)
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

std::optional<TargetRef> Model::firstTargetInCanvas(int canvasId, std::optional<int> preferredMonitorId) const {
    std::optional<TargetRef> preferredWindow;
    std::optional<TargetRef> preferredTarget;
    std::optional<TargetRef> firstWindow;
    std::optional<TargetRef> firstTargetInCanvasRef;

    for (const auto& node : targetGraph_) {
        const auto* target = resolve(node.ref);
        if (!target || target->canvasId != canvasId)
            continue;

        if (!firstTargetInCanvasRef)
            firstTargetInCanvasRef = node.ref;
        if (target->type == TargetType::Window && !firstWindow)
            firstWindow = node.ref;

        if (preferredMonitorId && target->monitorId == *preferredMonitorId) {
            if (!preferredTarget)
                preferredTarget = node.ref;
            if (target->type == TargetType::Window && !preferredWindow)
                preferredWindow = node.ref;
        }
    }

    if (preferredWindow)
        return preferredWindow;
    if (preferredTarget)
        return preferredTarget;
    if (firstWindow)
        return firstWindow;
    return firstTargetInCanvasRef;
}

std::optional<int> Model::findAdjacentCanvas(int canvasId, Direction direction) const {
    if (canvasGraph_.empty())
        return std::nullopt;

    std::vector<OverviewLogic::TargetCandidate> candidates;
    candidates.reserve(canvasGraph_.size());
    auto currentIndex = std::optional<size_t>{};
    for (size_t index = 0; index < canvasGraph_.size(); ++index) {
        const auto& canvas = canvasGraph_[index];
        candidates.push_back({
            .monitorId = 0,
            .box = canvas.box,
        });
        if (canvas.canvasId == canvasId)
            currentIndex = index;
    }

    if (!currentIndex)
        return std::nullopt;

    const auto nextIndex = OverviewLogic::pickTargetIndex(candidates, *currentIndex, direction);
    if (!nextIndex)
        return std::nullopt;

    return canvasGraph_[*nextIndex].canvasId;
}

std::optional<TargetRef> Model::firstTarget() const {
    if (targetGraph_.empty())
        return std::nullopt;

    return targetGraph_.front().ref;
}

void Model::rebuildCanvasGraph() {
    canvasGraph_.clear();

    for (const auto& region : monitors_) {
        for (const auto& workspace : region.workspaces) {
            const auto exists = std::any_of(canvasGraph_.begin(), canvasGraph_.end(), [&](const auto& canvas) {
                return canvas.canvasId == workspace.canvasId;
            });
            if (exists)
                continue;

            canvasGraph_.push_back({
                .canvasId = workspace.canvasId,
                .tileX = workspace.tileX,
                .tileY = workspace.tileY,
                .box = {
                    static_cast<double>(workspace.tileX) * 128.0,
                    static_cast<double>(workspace.tileY) * 128.0,
                    96.0,
                    96.0,
                },
                .synthetic = workspace.synthetic,
            });
        }
    }

    std::sort(canvasGraph_.begin(), canvasGraph_.end(), [](const auto& lhs, const auto& rhs) {
        if (lhs.tileY != rhs.tileY)
            return lhs.tileY < rhs.tileY;
        if (lhs.tileX != rhs.tileX)
            return lhs.tileX < rhs.tileX;
        return lhs.canvasId < rhs.canvasId;
    });
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
                    .canvasId = target.canvasId,
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
            .canvasId = syntheticSelection_->canvasId,
            .monitorId = syntheticSelection_->monitorId,
            .workspaceId = syntheticSelection_->workspaceId,
            .box = syntheticSelection_->box,
        });
    }
}

void Model::rebuild(const std::vector<CanvasLayoutState::SyntheticCanvasWorkspace>& synthetics, int viewportCanvasId) {
    // Rebuild is a full snapshot refresh:
    // 1. enumerate current monitors into empty regions
    // 2. ask the persistent canvas repository for the canvas-workspace graph
    // 3. attach one monitor-local workspace node per canvas
    // 4. lay out those nodes by canvas tile coordinates
    // 5. project targets into final preview geometry
    // 6. flatten the result into both target and canvas navigation graphs
    monitors_.clear();
    selectionRef_.reset();
    syntheticSelection_.reset();
    targetGraph_.clear();
    canvasGraph_.clear();

    if (!g_pCompositor)
        return;

    auto& canvasRepo = CanvasLayoutState::canvasRepository();
    canvasRepo.initialize();
    const auto activeCanvasId = canvasRepo.ensureCurrentVisibleCanvas();
    const auto previewCanvases = canvasRepo.previewCanvases(synthetics);
    const auto* anchorCanvas = find_canvas_record(previewCanvases,
                                                  viewportCanvasId != INVALID_CANVAS_ID ? viewportCanvasId : activeCanvasId);
    const auto anchorTileX = anchorCanvas ? anchorCanvas->tileX : 0;
    const auto anchorTileY = anchorCanvas ? anchorCanvas->tileY : 0;

    for (const auto& monitor : g_pCompositor->m_monitors) {
        if (!monitor)
            continue;

        monitors_.push_back(make_monitor_region(monitor));
    }

    std::unordered_map<WORKSPACEID, CanvasOverviewSnapshot> workspaceSnapshots;
    for (const auto& canvas : previewCanvases) {
        for (const auto& member : canvas.members) {
            if (member.workspaceId == INVALID_WORKSPACE_ID || workspaceSnapshots.contains(member.workspaceId))
                continue;

            auto* layout = CanvasLayoutInternal::get_canvas_for_workspace(member.workspaceId);
            if (layout) {
                workspaceSnapshots.emplace(member.workspaceId, layout->buildOverviewSnapshot());
                continue;
            }

            if (auto* gridLayout = grid_layout_for_workspace(member.workspaceId))
                workspaceSnapshots.emplace(member.workspaceId, gridLayout->buildOverviewSnapshot());
        }
    }

    for (auto& region : monitors_) {
        region.workspaces.reserve(previewCanvases.size());
        for (const auto& canvas : previewCanvases) {
            const auto* member = find_member(canvas, region.monitorId);
            const auto snapshotIt = member ? workspaceSnapshots.find(member->workspaceId) : workspaceSnapshots.end();
            region.workspaces.push_back(build_workspace_node(canvas,
                                                             region.monitorId,
                                                             member,
                                                             snapshotIt == workspaceSnapshots.end() ? nullptr : &snapshotIt->second,
                                                             is_synthetic_canvas(synthetics, canvas.canvasId)));
        }

        layoutWorkspaceGrid(region, anchorTileX, anchorTileY);
        for (auto& workspace : region.workspaces)
            project_workspace_targets(region.monitor, workspace);
    }

    rebuildCanvasGraph();
    rebuildTargetGraph();
}

} // namespace Overview
