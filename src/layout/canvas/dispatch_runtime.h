#pragma once

#include <string>

#include <hyprland/src/SharedDefs.hpp>
#include <hyprland/src/desktop/DesktopTypes.hpp>

#include "dispatch_logic.h"

namespace CanvasLayoutInternal {

// Runtime seam around dispatcher lookups and Hyprland focus/workspace state.
struct DispatcherRuntime : DispatcherRegistryRuntime {
    virtual PHLMONITOR getMonitorFromID(int monitorId) const = 0;
    virtual PHLMONITOR getMonitorFromCursor() const = 0;
    virtual bool isWindowActive(PHLWINDOW window) const = 0;

    virtual std::string monitorName(PHLMONITOR monitor) const = 0;
    virtual WORKSPACEID workspaceID(PHLWORKSPACE workspace) const = 0;
    virtual bool isWorkspaceSpecial(PHLWORKSPACE workspace) const = 0;
    virtual std::string workspaceSelector(PHLWORKSPACE workspace) const = 0;
    virtual bool activateMonitorWorkspaceDirectly(PHLMONITOR monitor, PHLWORKSPACE workspace, WORKSPACEID fallbackWorkspaceId) const = 0;
    virtual bool isWorkspaceActiveOnMonitor(PHLMONITOR monitor, PHLWORKSPACE workspace, WORKSPACEID fallbackWorkspaceId) const = 0;

    virtual MONITORID windowMonitorID(PHLWINDOW window) const = 0;
    virtual WORKSPACEID windowWorkspaceID(PHLWINDOW window) const = 0;
    virtual void warpCursorToWindow(PHLWINDOW window) const = 0;
};

// Replace the dispatcher runtime for in-process tests.
void set_dispatcher_runtime_for_tests(DispatcherRuntime* runtime);

// Logic entry points that accept an explicit runtime so tests can drive them.
bool focus_monitor_workspace(const DispatcherRuntime& runtime,
                             PHLMONITOR monitor,
                             PHLWORKSPACE workspace,
                             WORKSPACEID fallback_workspace_id,
                             bool require_monitor_focus = true,
                             const char* context = nullptr);
void focus_window_monitor(const DispatcherRuntime& runtime, PHLWINDOW window);
bool switch_to_window(const DispatcherRuntime& runtime, PHLWINDOW window, bool warp_cursor = false);

} // namespace CanvasLayoutInternal
