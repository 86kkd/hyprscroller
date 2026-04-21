/**
 * @file model_layout.cpp
 * @brief Pure workspace-grid helpers used during overview model construction.
 */
#include "model_layout.h"

#include <algorithm>
#include <vector>

namespace Overview {
namespace {

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
    target.synthetic = synthetic;
    return target;
}

void layoutWorkspaceGrid(MonitorRegion& region) {
    if (region.workspaces.empty())
        return;

    std::vector<int> workspaceIds;
    workspaceIds.reserve(region.workspaces.size());
    for (const auto& workspace : region.workspaces)
        workspaceIds.push_back(workspace.workspaceId);

    const auto cells = layoutWorkspaceGridCells(region.box, workspaceIds);
    std::sort(region.workspaces.begin(), region.workspaces.end(), [](const WorkspaceNode& a, const WorkspaceNode& b) {
        return a.workspaceId < b.workspaceId;
    });

    for (std::size_t index = 0; index < region.workspaces.size(); ++index) {
        auto& workspace = region.workspaces[index];
        workspace.box = cells[index].box;
        finalizeWorkspaceTargets(workspace);
    }
}

} // namespace Overview
