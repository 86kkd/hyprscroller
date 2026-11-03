/**
 * @file workspace_selector.h
 * @brief Shared conversion from workspace objects to Hyprland dispatcher args.
 */
#pragma once

#include <string>

#include "hyprland_runtime.h"

namespace ScrollerCore {

inline std::string workspace_selector(PHLWORKSPACE workspace) {
    if (!workspace)
        return {};

    if (!workspace->displayName().empty())
        return workspace->displayName();

    return std::to_string(ScrollerCore::HyprlandRuntime::workspaceId(workspace));
}

} // namespace ScrollerCore
