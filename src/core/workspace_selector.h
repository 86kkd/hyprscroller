/**
 * @file workspace_selector.h
 * @brief Shared conversion from workspace objects to Hyprland dispatcher args.
 */
#pragma once

#include <string>

#include <hyprland/src/desktop/Workspace.hpp>

namespace ScrollerCore {

inline std::string workspace_selector(PHLWORKSPACE workspace) {
    if (!workspace)
        return {};

    if (!workspace->m_name.empty())
        return workspace->m_name;

    return std::to_string(workspace->m_id);
}

} // namespace ScrollerCore
