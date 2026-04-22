/**
 * @file session_effects_runtime.h
 * @brief Runtime seam for overview accept/restore side-effect orchestration.
 */
#pragma once

#include <vector>

#include "model.h"

namespace Overview::SessionEffects {

struct Runtime {
    virtual ~Runtime() = default;

    virtual WORKSPACEID             currentWorkspaceId() const = 0;
    virtual PHLWORKSPACE            getWorkspaceByID(WORKSPACEID workspaceId) const = 0;
    virtual std::vector<PHLWORKSPACE> getWorkspaces() const = 0;
    virtual PHLMONITOR              getMonitorFromID(MONITORID monitorId) const = 0;
    virtual PHLMONITOR              getMonitorFromCursor() const = 0;

    virtual MONITORID               monitorId(PHLMONITOR monitor) const = 0;
    virtual WORKSPACEID             workspaceId(PHLWORKSPACE workspace) const = 0;
    virtual MONITORID               workspaceMonitorId(PHLWORKSPACE workspace) const = 0;
    virtual bool                    isWorkspaceSpecial(PHLWORKSPACE workspace) const = 0;
    virtual PHLWINDOW               workspaceLastFocusedWindow(PHLWORKSPACE workspace) const = 0;

    virtual MONITORID               windowMonitorId(PHLWINDOW window) const = 0;
    virtual bool                    isWindowMapped(PHLWINDOW window) const = 0;

    virtual PHLMONITOR              visibleMonitorForWorkspace(PHLWORKSPACE workspace) const = 0;
    virtual void                    syncCanvasTargetWindow(PHLWORKSPACE workspace, PHLWINDOW window, MONITORID monitorId) const = 0;
    virtual void                    prepareWorkspaceSnapshot(PHLWORKSPACE workspace) const = 0;
    virtual bool                    focusMonitorWorkspace(PHLMONITOR monitor, PHLWORKSPACE workspace, WORKSPACEID fallbackWorkspaceId,
                                                          bool requireMonitorFocus, const char* context) const = 0;
    virtual bool                    switchToWindow(PHLWINDOW window, bool warpCursor) const = 0;
};

OriginState captureOrigin(const Runtime& runtime);
void        prepareSnapshots(const Runtime& runtime);
WORKSPACEID nextWorkspaceId(const Runtime& runtime);
bool        acceptTarget(const Runtime& runtime, const Target& selection);
bool        restoreOrigin(const Runtime& runtime, const OriginState& origin);

} // namespace Overview::SessionEffects
