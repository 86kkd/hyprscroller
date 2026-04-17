/**
 * @file scene.cpp
 * @brief Mapping from logical overview model to render-ready monitor scenes.
 */
#include "scene.h"

#include <algorithm>
#include <span>

#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/desktop/Workspace.hpp>

#include "../core/layout_math.h"

namespace Overview {
namespace {

using ScrollerCore::Box;
using ScrollerCore::OverviewProjection;

Box localize_box(PHLMONITOR monitor, const Box& box) {
    if (!monitor)
        return box;

    return {
        box.x - monitor->m_position.x,
        box.y - monitor->m_position.y,
        box.w,
        box.h,
    };
}

bool target_matches_selection(const Target& target, const Target* selection) {
    if (!selection)
        return false;

    return target.type == selection->type
        && target.workspaceId == selection->workspaceId
        && target.monitorId == selection->monitorId
        && target.window == selection->window
        && target.synthetic == selection->synthetic;
}

std::string workspace_label(PHLWORKSPACE workspace, WORKSPACEID workspaceId) {
    if (!workspace)
        return std::to_string(workspaceId);

    if (!workspace->m_name.empty())
        return workspace->m_name;

    return std::to_string(workspace->m_id);
}

std::string empty_target_label(const Target& target) {
    return target.synthetic ? "new workspace " + std::to_string(target.workspaceId)
                            : "empty workspace";
}

std::string window_target_label(PHLWINDOW window) {
    if (!window)
        return "window";

    if (!window->m_title.empty())
        return window->m_title;

    return "window";
}

Box inset_box(const Box& box, double insetX, double insetY) {
    return {
        box.x + insetX,
        box.y + insetY,
        std::max(1.0, box.w - insetX * 2.0),
        std::max(1.0, box.h - insetY * 2.0),
    };
}

OverviewProjection compute_projection(std::span<const Box> items, const Box& visibleBox) {
    std::vector<ScrollerCore::OverviewRect> rects;
    rects.reserve(items.size());

    for (const auto& item : items) {
        rects.push_back({
            .x0 = item.x,
            .x1 = item.x + item.w,
            .y0 = item.y,
            .y1 = item.y + item.h,
        });
    }

    return ScrollerCore::compute_overview_projection(rects, visibleBox);
}

Box apply_projection(const Box& source, const Box& visibleBox, const OverviewProjection& projection) {
    if (projection.width <= 0.0 || projection.height <= 0.0)
        return visibleBox;

    return {
        visibleBox.x + projection.offset.x + (source.x - projection.min.x) * projection.scale,
        visibleBox.y + projection.offset.y + (source.y - projection.min.y) * projection.scale,
        std::max(24.0, source.w * projection.scale),
        std::max(24.0, source.h * projection.scale),
    };
}

SceneTarget build_empty_workspace_target(const Box& contentBox, const Target& target, const Target* selection) {
    return {
        .type = target.type,
        .box = inset_box(contentBox, std::max(12.0, contentBox.w * 0.12), std::max(12.0, contentBox.h * 0.14)),
        .synthetic = target.synthetic,
        .selected = target_matches_selection(target, selection),
        .label = empty_target_label(target),
    };
}

std::vector<SceneTarget> build_projected_targets(PHLMONITOR monitor, const WorkspaceNode& workspace,
                                                 const Box& contentBox, const Target* selection) {
    std::vector<Box> sourceBoxes;
    sourceBoxes.reserve(workspace.targets.size());
    for (const auto& target : workspace.targets)
        sourceBoxes.push_back(localize_box(monitor, target.box));

    const auto projection = compute_projection(sourceBoxes, contentBox);

    std::vector<SceneTarget> targets;
    targets.reserve(workspace.targets.size());
    for (std::size_t index = 0; index < workspace.targets.size(); ++index) {
        const auto& target = workspace.targets[index];
        targets.push_back({
            .type = target.type,
            .box = apply_projection(sourceBoxes[index], contentBox, projection),
            .synthetic = target.synthetic,
            .selected = target_matches_selection(target, selection),
            .label = target.type == TargetType::Window ? window_target_label(target.window) : empty_target_label(target),
        });
    }

    return targets;
}

} // namespace

std::optional<SceneMonitor> buildSceneForMonitor(PHLMONITOR monitor, const Model& model) {
    if (!monitor)
        return std::nullopt;

    const auto* region = model.regionForMonitor(monitor->m_id);
    if (!region)
        return std::nullopt;

    const auto* selection = model.selection();
    SceneMonitor scene;
    scene.monitorId = monitor->m_id;
    scene.monitorName = monitor->m_name;
    scene.box = {0.0, 0.0, monitor->m_size.x, monitor->m_size.y};

    for (const auto& workspace : region->workspaces) {
        SceneWorkspace sceneWorkspace;
        sceneWorkspace.box = localize_box(monitor, workspace.box);
        sceneWorkspace.contentBox = inset_box(sceneWorkspace.box, 14.0, 14.0);
        sceneWorkspace.contentBox.y += 26.0;
        sceneWorkspace.contentBox.h = std::max(36.0, sceneWorkspace.contentBox.h - 26.0);

        const auto workspaceRef = g_pCompositor->getWorkspaceByID(workspace.workspaceId);
        sceneWorkspace.special = workspaceRef ? workspaceRef->m_isSpecialWorkspace : false;
        sceneWorkspace.label = workspace_label(workspaceRef, workspace.workspaceId);

        const auto hasWindowTargets = std::any_of(workspace.targets.begin(), workspace.targets.end(), [](const Target& target) {
            return target.type == TargetType::Window && target.window;
        });

        if (hasWindowTargets) {
            sceneWorkspace.targets = build_projected_targets(monitor, workspace, sceneWorkspace.contentBox, selection);
        } else if (!workspace.targets.empty()) {
            sceneWorkspace.targets.push_back(build_empty_workspace_target(sceneWorkspace.contentBox, workspace.targets.front(), selection));
        }

        for (const auto& target : sceneWorkspace.targets) {
            if (!target.selected)
                continue;

            sceneWorkspace.selected = true;
            scene.selectionBox = target.box;
        }

        scene.workspaces.push_back(std::move(sceneWorkspace));
    }

    if (const auto synthetic = model.syntheticSelection(); synthetic && synthetic->monitorId == monitor->m_id) {
        SceneTarget syntheticTarget;
        syntheticTarget.type = synthetic->type;
        syntheticTarget.box = localize_box(monitor, synthetic->box);
        syntheticTarget.synthetic = true;
        syntheticTarget.selected = target_matches_selection(*synthetic, selection);
        syntheticTarget.label = empty_target_label(*synthetic);
        if (syntheticTarget.selected)
            scene.selectionBox = syntheticTarget.box;
        scene.syntheticTarget = std::move(syntheticTarget);
    }

    return scene;
}

} // namespace Overview
