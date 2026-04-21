/**
 * @file workspace_grid.cpp
 * @brief Pure workspace-grid helpers shared by overview model code and tests.
 */
#include "workspace_grid.h"

#include <algorithm>
#include <cmath>

namespace Overview {

WorkspaceGridShape chooseWorkspaceGridShape(const ScrollerCore::Box& regionBox, std::size_t count) {
    if (count <= 1)
        return {};

    const auto safeWidth = std::max(1.0, regionBox.w);
    const auto safeHeight = std::max(1.0, regionBox.h);
    const auto landscape = safeWidth >= safeHeight;
    const auto dominantAspect = landscape ? safeWidth / safeHeight : safeHeight / safeWidth;

    if (landscape) {
        const auto columns = std::min(count,
                                      std::max<std::size_t>(1, static_cast<std::size_t>(std::ceil(std::sqrt(static_cast<double>(count) * dominantAspect)))));
        const auto rows = std::max<std::size_t>(1, static_cast<std::size_t>(std::ceil(static_cast<double>(count) / static_cast<double>(columns))));
        return {.columns = columns, .rows = rows};
    }

    const auto rows = std::min(count,
                               std::max<std::size_t>(1, static_cast<std::size_t>(std::ceil(std::sqrt(static_cast<double>(count) * dominantAspect)))));
    const auto columns = std::max<std::size_t>(1, static_cast<std::size_t>(std::ceil(static_cast<double>(count) / static_cast<double>(rows))));
    return {.columns = columns, .rows = rows};
}

std::vector<WorkspaceGridCell> layoutWorkspaceGridCells(const ScrollerCore::Box& regionBox, std::vector<int> workspaceIds) {
    std::sort(workspaceIds.begin(), workspaceIds.end());

    std::vector<WorkspaceGridCell> cells;
    cells.reserve(workspaceIds.size());
    if (workspaceIds.empty())
        return cells;

    if (workspaceIds.size() == 1) {
        cells.push_back({
            .workspaceId = workspaceIds.front(),
            .box = regionBox,
        });
        return cells;
    }

    const auto grid = chooseWorkspaceGridShape(regionBox, workspaceIds.size());
    const auto horizontalGap = std::min(32.0, std::max(12.0, regionBox.w * 0.02));
    const auto verticalGap = std::min(32.0, std::max(12.0, regionBox.h * 0.03));
    const auto totalHorizontalGap = horizontalGap * static_cast<double>(grid.columns - 1);
    const auto totalVerticalGap = verticalGap * static_cast<double>(grid.rows - 1);
    const auto cellWidth = std::max(120.0, (regionBox.w - totalHorizontalGap) / static_cast<double>(grid.columns));
    const auto cellHeight = std::max(96.0, (regionBox.h - totalVerticalGap) / static_cast<double>(grid.rows));

    for (std::size_t index = 0; index < workspaceIds.size(); ++index) {
        const auto column = index % grid.columns;
        const auto row = index / grid.columns;
        cells.push_back({
            .workspaceId = workspaceIds[index],
            .box = {
                regionBox.x + static_cast<double>(column) * (cellWidth + horizontalGap),
                regionBox.y + static_cast<double>(row) * (cellHeight + verticalGap),
                cellWidth,
                cellHeight,
            },
        });
    }

    return cells;
}

} // namespace Overview
