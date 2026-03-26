/**
 * @file session.h
 * @brief Global logical overview session spanning all canvases and monitors.
 *
 * The overview session is a navigation-only layer. Real windows remain owned by
 * their per-workspace `CanvasLayout`; overview builds a temporary logical map
 * of windows and empty-workspace targets so directional navigation can happen
 * without mutating real Hyprland focus on every keypress.
 */
#pragma once

#include <optional>
#include <vector>

#include <hyprland/src/SharedDefs.hpp>
#include <hyprland/src/desktop/DesktopTypes.hpp>
#include <hyprland/src/macros.hpp>

#include "../core/direction.h"
#include "../core/types.h"

namespace Overview {

enum class TargetType {
    Window,
    EmptyWorkspace,
};

struct Target {
    TargetType         type = TargetType::Window;
    WORKSPACEID        workspaceId = WORKSPACE_INVALID;
    int                monitorId = MONITOR_INVALID;
    PHLWINDOW          window = nullptr;
    ScrollerCore::Box  box;
    bool               synthetic = false;
};

struct WorkspaceNode {
    WORKSPACEID         workspaceId = WORKSPACE_INVALID;
    int                 monitorId = MONITOR_INVALID;
    std::vector<Target> targets;
    ScrollerCore::Box   box;
};

struct MonitorRegion {
    int                       monitorId = MONITOR_INVALID;
    PHLMONITOR                monitor = nullptr;
    ScrollerCore::Box         box;
    std::vector<WorkspaceNode> workspaces;
};

class Session {
  public:
    bool active() const;
    void open();
    void close(bool acceptSelectionFlag);
    bool moveSelection(Direction direction);
    const std::vector<MonitorRegion>& monitors() const;
    const std::optional<Target>& selection() const;
    void damageMonitors() const;

  private:
    void rebuild();
    void clear();
    bool selectInitialTarget();
    void acceptSelection();
    bool createSyntheticEmptyTarget(Direction direction);

    std::vector<const Target*> collectTargets() const;
    const Target*              findBestTarget(Direction direction) const;
    const MonitorRegion*       regionForMonitor(int monitorId) const;
    WORKSPACEID                nextWorkspaceId() const;

    bool                  active_ = false;
    WORKSPACEID           originWorkspace_ = WORKSPACE_INVALID;
    PHLWINDOW             originWindow_ = nullptr;
    std::vector<MonitorRegion> monitors_;
    std::optional<Target> selection_;
    std::optional<Target> syntheticEmptyTarget_;
};

Session& session();

} // namespace Overview
