/**
 * @file overview/model/layout.cpp
 * @brief Pure workspace-grid helpers used during overview model construction.
 */
#include "overview/model/layout.h"

#include <algorithm>
#include <vector>

namespace Overview {
namespace {

// Empty-workspace targets are inset slightly so they read as a tile inside the
// workspace region rather than being mistaken for the full region background.
ScrollerCore::Box inset_box(const ScrollerCore::Box& box, double ratio, double minimumInset = 18.0) {
    const auto insetX = std::min(std::max(minimumInset, box.w * ratio), std::max(0.0, box.w / 2.5));
    const auto insetY = std::min(std::max(minimumInset, box.h * ratio), std::max(0.0, box.h / 2.5));
    return {
        box.x + insetX,
        box.y + insetY,
        std::max(24.0, box.w - insetX * 2.0),
        std::max(24.0, box.h - insetY * 2.0),
    };
}

void finalizeWorkspaceTargets(WorkspaceNode& node) {
    if (!node.targets.empty())
        return;

    // A workspace with no real windows still needs a selectable target so
    // directional navigation and "accept" keep working uniformly.
    node.targets.push_back(makeEmptyTarget(node.workspaceId, node.monitorId, node.box, false));
}

} // namespace

Target makeEmptyTarget(WORKSPACEID workspaceId, int monitorId, const ScrollerCore::Box& workspaceBox, bool synthetic) {
    Target target;
    target.type = TargetType::EmptyWorkspace;
    target.workspaceId = workspaceId;
    target.monitorId = monitorId;
    target.window = nullptr;
    target.box = inset_box(workspaceBox, synthetic ? 0.16 : 0.20);
    target.sourceBox = target.box;
    target.synthetic = synthetic;
    return target;
}

void layoutWorkspaceGrid(MonitorRegion& region) {
    if (region.workspaces.empty())
        return;

    // Grid helpers operate on workspace ids only; the richer WorkspaceNode data
    // is stitched back in once grid cells have been chosen.
    std::vector<int> workspaceIds;
    workspaceIds.reserve(region.workspaces.size());
    for (const auto& workspace : region.workspaces)
        workspaceIds.push_back(workspace.workspaceId);

    const auto cells = layoutWorkspaceGridCells(region.box, workspaceIds);
    std::sort(region.workspaces.begin(), region.workspaces.end(), [](const WorkspaceNode& a, const WorkspaceNode& b) {
        return a.workspaceId < b.workspaceId;
    });

    // After boxes are assigned, ensure every workspace exposes at least one
    // selectable target, even if that target is synthetic/empty.
    for (std::size_t index = 0; index < region.workspaces.size(); ++index) {
        auto& workspace = region.workspaces[index];
        workspace.box = cells[index].box;
        finalizeWorkspaceTargets(workspace);
    }
}

} // namespace Overview
