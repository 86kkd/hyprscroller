#include <cmath>
#include <limits>
#include <optional>

#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/desktop/Workspace.hpp>
#include <hyprland/src/helpers/Monitor.hpp>
#include <hyprland/src/layout/algorithm/Algorithm.hpp>
#include <hyprland/src/layout/space/Space.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <spdlog/spdlog.h>

#include "internal.h"

namespace {
PHLMONITOR effective_workspace_monitor(Lane* lane, PHLMONITOR monitor, PHLWORKSPACE workspace) {
    if (!lane || !monitor || !workspace || !workspace->m_isSpecialWorkspace)
        return monitor;

    const auto active_window = lane->get_active_window();
    if (!active_window)
        return monitor;

    const auto active_monitor = g_pCompositor->getMonitorFromID(active_window->monitorID());
    if (!active_monitor || active_monitor == monitor)
        return monitor;

    spdlog::debug("recalculate_workspace_lane: overriding special workspace monitor workspace={} from_monitor={} to_monitor={} active_window={}",
                  workspace->m_id,
                  monitor->m_id,
                  active_monitor->m_id,
                  static_cast<const void*>(active_window.get()));
    return active_monitor;
}

double primary_cross_monitor_score(PHLWINDOW window, PHLMONITOR monitor, Direction direction) {
    const auto window_left = window->m_position.x;
    const auto window_right = window->m_position.x + window->m_size.x;
    const auto window_top = window->m_position.y;
    const auto window_bottom = window->m_position.y + window->m_size.y;
    const auto monitor_left = monitor->m_position.x;
    const auto monitor_right = monitor->m_position.x + monitor->m_size.x;
    const auto monitor_top = monitor->m_position.y;
    const auto monitor_bottom = monitor->m_position.y + monitor->m_size.y;

    switch (direction) {
        case Direction::Left:
            return std::abs(window_right - monitor_right);
        case Direction::Right:
            return std::abs(window_left - monitor_left);
        case Direction::Up:
            return std::abs(window_bottom - monitor_bottom);
        case Direction::Down:
            return std::abs(window_top - monitor_top);
        default:
            return std::numeric_limits<double>::infinity();
    }
}

double secondary_cross_monitor_score(PHLWINDOW window, PHLWINDOW source_window, Direction direction) {
    if (!source_window)
        return 0.0;

    const auto window_middle = window->middle();
    const auto source_middle = source_window->middle();
    switch (direction) {
        case Direction::Left:
        case Direction::Right:
            return std::abs(window_middle.y - source_middle.y);
        case Direction::Up:
        case Direction::Down:
            return std::abs(window_middle.x - source_middle.x);
        default:
            return 0.0;
    }
}
} // namespace

namespace CanvasLayoutInternal {
const char* direction_name(Direction direction) {
    switch (direction) {
        case Direction::Left: return "left";
        case Direction::Right: return "right";
        case Direction::Up: return "up";
        case Direction::Down: return "down";
        case Direction::Begin: return "begin";
        case Direction::End: return "end";
        case Direction::Center: return "center";
        default: return "unknown";
    }
}

void recalculate_workspace_lane(Lane* lane, PHLMONITOR monitor, PHLWORKSPACE workspace, bool honor_fullscreen) {
    if (!lane || !monitor || !workspace)
        return;

    monitor = effective_workspace_monitor(lane, monitor, workspace);
    lane->update_sizes(monitor);
    if (honor_fullscreen && workspace->m_hasFullscreenWindow && workspace->m_fullscreenMode == FSMODE_FULLSCREEN) {
        lane->set_fullscreen_active_window();
        return;
    }

    lane->recalculate_lane_geometry();
}

WORKSPACEID preferred_workspace_id(PHLMONITOR monitor, WORKSPACEID) {
    if (!monitor)
        return WORKSPACE_INVALID;

    const auto special_workspace_id = monitor->activeSpecialWorkspaceID();
    if (g_pCompositor->getWorkspaceByID(special_workspace_id))
        return special_workspace_id;

    return monitor->activeWorkspaceID();
}

PHLMONITOR visible_monitor_for_workspace(PHLWORKSPACE workspace) {
    if (!workspace)
        return nullptr;

    if (!workspace->m_isSpecialWorkspace)
        return g_pCompositor->getMonitorFromID(workspace->monitorID());

    for (const auto& monitor : g_pCompositor->m_monitors) {
        if (monitor && monitor->activeSpecialWorkspaceID() == workspace->m_id)
            return monitor;
    }

    return nullptr;
}

CanvasLayout* get_canvas_for_workspace(const WORKSPACEID workspace_id) {
    const auto workspace = g_pCompositor->getWorkspaceByID(workspace_id);
    if (!workspace || !workspace->m_space)
        return nullptr;

    const auto algorithm = workspace->m_space->algorithm();
    if (!algorithm)
        return nullptr;

    const auto& tiled = algorithm->tiledAlgo();
    if (!tiled)
        return nullptr;

    return dynamic_cast<CanvasLayout*>(tiled.get());
}

std::optional<Math::eDirection> direction_to_math(Direction direction) {
    switch (direction) {
        case Direction::Left:
            return Math::fromChar('l');
        case Direction::Right:
            return Math::fromChar('r');
        case Direction::Up:
            return Math::fromChar('u');
        case Direction::Down:
            return Math::fromChar('d');
        default:
            return std::nullopt;
    }
}

PHLWINDOW pick_cross_monitor_target_window(PHLMONITOR monitor, WORKSPACEID workspace_id, Direction direction, PHLWINDOW source_window) {
    PHLWINDOW best = nullptr;
    auto best_primary = std::numeric_limits<double>::infinity();
    auto best_secondary = std::numeric_limits<double>::infinity();

    for (const auto& window : g_pCompositor->m_windows) {
        if (!window || window->workspaceID() != workspace_id || window->m_isFloating || !window->m_isMapped || window->isHidden())
            continue;

        const auto primary = primary_cross_monitor_score(window, monitor, direction);
        const auto secondary = secondary_cross_monitor_score(window, source_window, direction);
        if (!best || primary < best_primary || (primary == best_primary && secondary < best_secondary)) {
            best = window;
            best_primary = primary;
            best_secondary = secondary;
        }
    }

    return best;
}

int get_workspace_id() {
    const auto monitor = monitorFromPointingOrCursor();
    if (!monitor)
        return -1;

    const auto workspace_id = preferred_workspace_id(monitor, monitor->activeSpecialWorkspaceID());
    if (workspace_id == WORKSPACE_INVALID)
        return -1;
    if (g_pCompositor->getWorkspaceByID(workspace_id) == nullptr)
        return -1;

    return workspace_id;
}
} // namespace CanvasLayoutInternal

void CanvasLayout::recalculateMonitor(const int &monitor_id)
{
    const auto workspace = getCanvasWorkspace();
    if (!workspace)
        return;

    const auto monitor = CanvasLayoutInternal::visible_monitor_for_workspace(workspace);
    if (!monitor || monitor->m_id != monitor_id)
        return;

    g_pHyprRenderer->damageMonitor(monitor);
    for (auto lane = lanes.first(); lane != nullptr; lane = lane->next())
        CanvasLayoutInternal::recalculate_workspace_lane(lane->data(), monitor, workspace, !workspace->m_isSpecialWorkspace);
}
