/**
 * @file scene.h
 * @brief Render-facing overview scene derived from the logical overview model.
 */
#pragma once

#include <optional>
#include <string>
#include <vector>

#include <hyprland/src/helpers/Monitor.hpp>

#include "core/types.h"
#include "overview/model/model.h"

namespace Overview {

struct SceneTarget {
    TargetType        type = TargetType::Window;
    ScrollerCore::Box box;
    PHLWINDOW         window = nullptr;
    bool              synthetic = false;
    bool              selected = false;
    std::string       label;
};

struct SceneWorkspace {
    ScrollerCore::Box      box;
    ScrollerCore::Box      contentBox;
    bool                   selected = false;
    bool                   special = false;
    std::string            label;
    std::vector<SceneTarget> targets;
};

struct SceneMonitor {
    int                      monitorId = INVALID_MONITOR_ID;
    std::string              monitorName;
    ScrollerCore::Box        box;
    std::vector<SceneWorkspace> workspaces;
    std::optional<SceneTarget> syntheticTarget;
    std::optional<ScrollerCore::Box> selectionBox;
};

std::optional<SceneMonitor> buildSceneForMonitor(PHLMONITOR monitor, const Model& model);

} // namespace Overview
