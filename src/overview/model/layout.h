/**
 * @file overview/model/layout.h
 * @brief Pure workspace-grid helpers used during overview model construction.
 */
#pragma once

#include "overview/model/model.h"
#include "overview/model/workspace_grid.h"

namespace Overview {

void layoutWorkspaceGrid(MonitorRegion& region, int anchorTileX, int anchorTileY);

} // namespace Overview
