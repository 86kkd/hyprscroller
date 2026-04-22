/**
 * @file scene.cpp
 * @brief Mapping from logical overview model to render-ready monitor scenes.
 *
 * `Model` still speaks in workspace/monitor/global geometry terms. This file
 * converts that data into render-facing DTOs:
 * - boxes are localized into per-monitor render space
 * - window targets are projected into workspace preview boxes
 * - selection state is copied into simple booleans and one selection outline box
 *
 * No Hyprland focus or overview state is mutated here; this is a pure
 * translation layer from logical model to render scene.
 */
#include "scene.h"

#include <algorithm>
#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/desktop/Workspace.hpp>

#include "geometry_utils.h"
#include "scene_layout.h"
#include "style.h"

namespace Overview {
namespace {

using ScrollerCore::Box;

// The render pass draws each monitor in local render coordinates, so convert
// global model boxes into one monitor-local space before scene projection.
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

// Scene objects do not hold `TargetRef`s. Recompute the same identity test in
// render-friendly form so the scene can cheaply mark selection state.
bool target_matches_selection(const Target& target, const Target* selection) {
    if (!selection)
        return false;

    return target.type == selection->type
        && target.workspaceId == selection->workspaceId
        && target.monitorId == selection->monitorId
        && target.window == selection->window
        && target.synthetic == selection->synthetic;
}

// Scene labels are intentionally lightweight and user-facing. They are not part
// of the selection logic; they only exist so render code can draw readable tags.
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

SceneTarget build_scene_target(PHLMONITOR monitor, const Target& target, const Target* selection) {
    return {
        .type = target.type,
        .box = localize_box(monitor, target.box),
        .window = target.window,
        .synthetic = target.synthetic,
        .selected = target_matches_selection(target, selection),
        .label = target.type == TargetType::Window ? window_target_label(target.window) : empty_target_label(target),
    };
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

    // Phase 1: convert each logical workspace into one render workspace card.
    for (const auto& workspace : region->workspaces) {
        SceneWorkspace sceneWorkspace;
        sceneWorkspace.box = localize_box(monitor, workspace.box);
        sceneWorkspace.contentBox = buildWorkspaceContentBox(sceneWorkspace.box);

        const auto workspaceRef = g_pCompositor->getWorkspaceByID(workspace.workspaceId);
        sceneWorkspace.special = workspaceRef ? workspaceRef->m_isSpecialWorkspace : false;
        sceneWorkspace.label = workspace_label(workspaceRef, workspace.workspaceId);

        sceneWorkspace.targets.reserve(workspace.targets.size());
        for (const auto& target : workspace.targets)
            sceneWorkspace.targets.push_back(build_scene_target(monitor, target, selection));

        // Track one selection outline box per monitor so render code can draw a
        // single highlighted border without re-walking the logical model.
        for (const auto& target : sceneWorkspace.targets) {
            if (!target.selected)
                continue;

            sceneWorkspace.selected = true;
            scene.selectionBox = target.box;
        }

        scene.workspaces.push_back(std::move(sceneWorkspace));
    }

    // Phase 2: synthetic targets bypass workspace cards and render directly on
    // the owning monitor, so append them after normal workspace processing.
    if (const auto synthetic = model.syntheticSelection(); synthetic && synthetic->monitorId == monitor->m_id) {
        SceneTarget syntheticTarget;
        syntheticTarget.type = synthetic->type;
        syntheticTarget.box = localize_box(monitor, synthetic->box);
        syntheticTarget.window = synthetic->window;
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
