/**
 * @file model.h
 * @brief Temporary overview model built from per-workspace canvas snapshots.
 */
#pragma once

#include <cstddef>
#include <optional>
#include <vector>

#include <hyprland/src/SharedDefs.hpp>
#include <hyprland/src/desktop/DesktopTypes.hpp>

#include "core/types.h"

namespace Overview {

constexpr WORKSPACEID INVALID_WORKSPACE_ID = static_cast<WORKSPACEID>(-1);
constexpr int         INVALID_MONITOR_ID = -1;

enum class TargetType {
    Window,
    EmptyWorkspace,
};

struct Target {
    TargetType        type = TargetType::Window;
    WORKSPACEID       workspaceId = INVALID_WORKSPACE_ID;
    int               monitorId = INVALID_MONITOR_ID;
    PHLWINDOW         window = nullptr;
    ScrollerCore::Box box;
    bool              synthetic = false;
};

struct WorkspaceNode {
    WORKSPACEID        workspaceId = INVALID_WORKSPACE_ID;
    int                monitorId = INVALID_MONITOR_ID;
    std::vector<Target> targets;
    ScrollerCore::Box  box;
};

struct MonitorRegion {
    int                      monitorId = INVALID_MONITOR_ID;
    PHLMONITOR               monitor = nullptr;
    ScrollerCore::Box        box;
    std::vector<WorkspaceNode> workspaces;
};

struct OriginState {
    int         monitorId = INVALID_MONITOR_ID;
    WORKSPACEID workspaceId = INVALID_WORKSPACE_ID;
    PHLWINDOW   window = nullptr;
};

struct TargetRef {
    bool        synthetic = false;
    std::size_t monitorIndex = 0;
    std::size_t workspaceIndex = 0;
    std::size_t targetIndex = 0;

    auto operator<=>(const TargetRef&) const = default;
};

struct TargetGraphNode {
    TargetRef          ref;
    int                monitorId = INVALID_MONITOR_ID;
    WORKSPACEID        workspaceId = INVALID_WORKSPACE_ID;
    ScrollerCore::Box  box;
};

Target makeEmptyTarget(WORKSPACEID workspaceId, int monitorId, const ScrollerCore::Box& workspaceBox, bool synthetic);

class Model {
  public:
    void clear();
    void rebuild();

    void setOrigin(int monitorId, WORKSPACEID workspaceId, PHLWINDOW window);
    const OriginState& origin() const;

    const std::vector<MonitorRegion>& monitors() const;
    const std::vector<TargetGraphNode>& targetGraph() const;

    const std::optional<TargetRef>& selectionRef() const;
    const Target* selection() const;
    void setSelection(const TargetRef& ref);
    void clearSelection();

    void setSyntheticSelection(Target target);
    void clearSyntheticSelection();
    const std::optional<Target>& syntheticSelection() const;

    const MonitorRegion* regionForMonitor(int monitorId) const;
    const Target* resolve(const TargetRef& ref) const;
    std::optional<TargetRef> findByWindow(PHLWINDOW window) const;
    std::optional<TargetRef> findByWorkspace(WORKSPACEID workspaceId) const;
    std::optional<TargetRef> firstTarget() const;

  private:
    void rebuildTargetGraph();

    OriginState                   origin_;
    std::vector<MonitorRegion>    monitors_;
    std::vector<TargetGraphNode>  targetGraph_;
    std::optional<TargetRef>      selectionRef_;
    std::optional<Target>         syntheticSelection_;
};

} // namespace Overview
