/**
 * @file overview/model/workspace_grid.h
 * @brief Pure workspace-grid helpers shared by overview model code and tests.
 */
#pragma once

#include <cstddef>
#include <vector>

#include "core/types.h"

namespace Overview {

struct WorkspaceGridShape {
    std::size_t columns = 1;
    std::size_t rows = 1;
};

struct WorkspaceGridCell {
    int               workspaceId = -1;
    ScrollerCore::Box box;
};

WorkspaceGridShape              chooseWorkspaceGridShape(const ScrollerCore::Box& regionBox, std::size_t count);
std::vector<WorkspaceGridCell>  layoutWorkspaceGridCells(const ScrollerCore::Box& regionBox, std::vector<int> workspaceIds);

} // namespace Overview
