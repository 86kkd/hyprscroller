#pragma once

#include <vector>

#include "types.h"

struct CanvasOverviewSnapshotWindow {
    PHLWINDOW         window = nullptr;
    ScrollerCore::Box box;
};

struct CanvasOverviewSnapshot {
    WORKSPACEID                               workspaceId = WORKSPACE_INVALID;
    int                                       monitorId = MONITOR_INVALID;
    std::vector<CanvasOverviewSnapshotWindow> windows;
};
