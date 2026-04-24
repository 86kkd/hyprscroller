#pragma once

#include <optional>
#include <string>
#include <vector>

#include <hyprland/src/SharedDefs.hpp>

#include "../../core/canvas_workspace_snapshot.h"
#include "../../core/direction.h"
#include "../../core/types.h"

namespace CanvasLayoutState {

struct CanvasWorkspaceMember {
    int         monitorId = -1;
    WORKSPACEID workspaceId = static_cast<WORKSPACEID>(-1);
    bool        special = false;
};

struct CanvasWorkspaceRecord {
    int                                canvasId = -1;
    int                                tileX = 0;
    int                                tileY = 0;
    std::vector<CanvasWorkspaceMember> members;
};

struct SyntheticCanvasWorkspace {
    CanvasWorkspaceRecord canvas;
    int                   anchorCanvasId = -1;
    Direction             direction = Direction::Right;
};

class CanvasWorkspaceRepository {
  public:
    void initialize();
    void flush() const;

    int activeCanvasId() const;
    std::vector<CanvasWorkspaceRecord> canvases() const;
    std::optional<CanvasWorkspaceRecord> find(int canvasId) const;

    std::vector<CanvasWorkspaceMember> currentVisibleMembers() const;
    int ensureCurrentVisibleCanvas();

    SyntheticCanvasWorkspace buildSyntheticCanvas(Direction direction,
                                                 int anchorCanvasId,
                                                 const std::vector<SyntheticCanvasWorkspace>& existingSynthetics = {}) const;
    std::vector<CanvasWorkspaceRecord> previewCanvases(const std::vector<SyntheticCanvasWorkspace>& synthetics = {}) const;
    void commitSyntheticCanvases(const std::vector<SyntheticCanvasWorkspace>& synthetics, int activeCanvasId);

    void markActive(int canvasId);
    void ensureCanvasHasVisibleMembers(int canvasId);

  private:
    std::string repositoryPath() const;
    void loadFromDisk();
    void flushToDisk() const;

    bool initialized_ = false;
    ScrollerCanvasSnapshot::RepositorySnapshot snapshot_;
};

CanvasWorkspaceRepository& canvasRepository();

} // namespace CanvasLayoutState
