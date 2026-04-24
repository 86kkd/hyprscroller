/**
 * @file overview/model/layout.cpp
 * @brief Pure workspace-grid helpers used during overview model construction.
 */
#include "overview/model/layout.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace Overview {
namespace {

void finalizeWorkspaceTargets(WorkspaceNode& node) {
    if (!node.targets.empty())
        return;

    // A workspace with no real windows still needs a selectable target so
    // directional navigation and "accept" keep working uniformly.
    node.targets.push_back(makeEmptyTarget(node.canvasId,
                                           node.workspaceId,
                                           node.monitorId,
                                           node.specialWorkspace,
                                           node.box,
                                           node.synthetic));
}

void layoutCanvasTiles(MonitorRegion& region, int anchorTileX, int anchorTileY) {
    const auto horizontalStride = region.box.w + std::max(48.0, region.box.w * 0.08);
    const auto verticalStride = region.box.h + std::max(48.0, region.box.h * 0.08);

    std::sort(region.workspaces.begin(), region.workspaces.end(), [](const WorkspaceNode& lhs, const WorkspaceNode& rhs) {
        if (lhs.tileY != rhs.tileY)
            return lhs.tileY < rhs.tileY;
        if (lhs.tileX != rhs.tileX)
            return lhs.tileX < rhs.tileX;
        if (lhs.canvasId != rhs.canvasId)
            return lhs.canvasId < rhs.canvasId;
        return lhs.workspaceId < rhs.workspaceId;
    });

    for (auto& workspace : region.workspaces) {
        const auto column = static_cast<double>(workspace.tileX - anchorTileX);
        const auto row = static_cast<double>(workspace.tileY - anchorTileY);
        workspace.box = {
            region.box.x + column * horizontalStride,
            region.box.y + row * verticalStride,
            region.box.w,
            region.box.h,
        };
        finalizeWorkspaceTargets(workspace);
    }
}

} // namespace

Target makeEmptyTarget(int canvasId,
                       WORKSPACEID workspaceId,
                       int monitorId,
                       bool specialWorkspace,
                       const ScrollerCore::Box& workspaceBox,
                       bool synthetic) {
    Target target;
    target.type = TargetType::EmptyWorkspace;
    target.canvasId = canvasId;
    target.workspaceId = workspaceId;
    target.monitorId = monitorId;
    target.specialWorkspace = specialWorkspace;
    target.window = nullptr;
    target.box = workspaceBox;
    target.sourceBox = target.box;
    target.synthetic = synthetic;
    return target;
}

void layoutWorkspaceGrid(MonitorRegion& region, int anchorTileX, int anchorTileY) {
    if (region.workspaces.empty())
        return;

    layoutCanvasTiles(region, anchorTileX, anchorTileY);
}

} // namespace Overview
