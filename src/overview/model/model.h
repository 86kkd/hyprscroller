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
#include "layout/canvas/canvas_workspace_repository.h"

namespace Overview {

constexpr WORKSPACEID INVALID_WORKSPACE_ID = static_cast<WORKSPACEID>(-1);
constexpr int         INVALID_MONITOR_ID = -1;
constexpr int         INVALID_CANVAS_ID = -1;

enum class TargetType {
    Window,
    EmptyWorkspace,
};

struct Target {
    TargetType        type = TargetType::Window;
    int               canvasId = INVALID_CANVAS_ID;
    WORKSPACEID       workspaceId = INVALID_WORKSPACE_ID;
    int               monitorId = INVALID_MONITOR_ID;
    bool              specialWorkspace = false;
    PHLWINDOW         window = nullptr;
    ScrollerCore::Box box;
    ScrollerCore::Box sourceBox;
    bool              synthetic = false;
};

struct WorkspaceNode {
    int                canvasId = INVALID_CANVAS_ID;
    int                tileX = 0;
    int                tileY = 0;
    WORKSPACEID        workspaceId = INVALID_WORKSPACE_ID;
    int                monitorId = INVALID_MONITOR_ID;
    bool               specialWorkspace = false;
    bool               synthetic = false;
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
    int                canvasId = INVALID_CANVAS_ID;
    int                monitorId = INVALID_MONITOR_ID;
    WORKSPACEID        workspaceId = INVALID_WORKSPACE_ID;
    ScrollerCore::Box  box;
};

struct CanvasGraphNode {
    int               canvasId = INVALID_CANVAS_ID;
    int               tileX = 0;
    int               tileY = 0;
    ScrollerCore::Box box;
    bool              synthetic = false;
};

Target makeEmptyTarget(int canvasId, WORKSPACEID workspaceId, int monitorId, bool specialWorkspace,
                       const ScrollerCore::Box& workspaceBox, bool synthetic);

class Model {
  public:
    void clear();
    void rebuild(const std::vector<CanvasLayoutState::SyntheticCanvasWorkspace>& synthetics = {},
                 int viewportCanvasId = INVALID_CANVAS_ID);

    void setOrigin(int monitorId, WORKSPACEID workspaceId, PHLWINDOW window);
    const OriginState& origin() const;

    const std::vector<MonitorRegion>& monitors() const;
    const std::vector<TargetGraphNode>& targetGraph() const;
    const std::vector<CanvasGraphNode>& canvasGraph() const;

    const std::optional<TargetRef>& selectionRef() const;
    const Target* selection() const;
    std::optional<int> selectionCanvasId() const;
    void setSelection(const TargetRef& ref);
    void clearSelection();

    void setSyntheticSelection(Target target);
    void clearSyntheticSelection();
    const std::optional<Target>& syntheticSelection() const;

    const MonitorRegion* regionForMonitor(int monitorId) const;
    const Target* resolve(const TargetRef& ref) const;
    std::optional<TargetRef> findByWindow(PHLWINDOW window) const;
    std::optional<TargetRef> findByWorkspace(WORKSPACEID workspaceId) const;
    std::optional<TargetRef> firstTargetInCanvas(int canvasId, std::optional<int> preferredMonitorId = std::nullopt) const;
    std::optional<int> findAdjacentCanvas(int canvasId, Direction direction) const;
    std::optional<TargetRef> firstTarget() const;

  private:
    void rebuildTargetGraph();
    void rebuildCanvasGraph();

    OriginState                   origin_;
    std::vector<MonitorRegion>    monitors_;
    std::vector<TargetGraphNode>  targetGraph_;
    std::vector<CanvasGraphNode>  canvasGraph_;
    std::optional<TargetRef>      selectionRef_;
    std::optional<Target>         syntheticSelection_;
};

} // namespace Overview
