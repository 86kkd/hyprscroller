/**
 * @file workspace_grid.cpp
 * @brief Pure workspace-grid helpers shared by overview model code and tests.
 */
#include "workspace_grid.h"

#include <algorithm>
#include <cmath>

namespace Overview {

namespace {

double fit_gap_to_region(double preferredGap, double regionSpan, std::size_t cellCount, double preferredMinCellSpan) {
    if (cellCount <= 1)
        return 0.0;

    const auto remainingAfterPreferredCells = regionSpan - preferredMinCellSpan * static_cast<double>(cellCount);
    if (remainingAfterPreferredCells <= 0.0)
        return 0.0;

    return std::min(preferredGap, remainingAfterPreferredCells / static_cast<double>(cellCount - 1));
}

}

WorkspaceGridShape chooseWorkspaceGridShape(const ScrollerCore::Box& regionBox, std::size_t count) {
    if (count <= 1)
        return {};

    const auto safeWidth = std::max(1.0, regionBox.w);
    const auto safeHeight = std::max(1.0, regionBox.h);
    const auto landscape = safeWidth >= safeHeight;
    const auto dominantAspect = landscape ? safeWidth / safeHeight : safeHeight / safeWidth;

    // The square-root heuristic gives a near-square grid, then biases the
    // longer axis to follow the monitor region's dominant aspect ratio.
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
    // Stable ordering keeps the same workspace ids in the same grid positions
    // across rebuilds, which reduces selection jumps during overview refreshes.
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
    // Gaps scale gently with monitor size but are clamped so small monitors
    // still breathe and large monitors do not waste too much space.
    const auto preferredHorizontalGap = std::min(32.0, std::max(12.0, regionBox.w * 0.02));
    const auto preferredVerticalGap = std::min(32.0, std::max(12.0, regionBox.h * 0.03));
    const auto horizontalGap = fit_gap_to_region(preferredHorizontalGap, regionBox.w, grid.columns, 120.0);
    const auto verticalGap = fit_gap_to_region(preferredVerticalGap, regionBox.h, grid.rows, 96.0);
    const auto totalHorizontalGap = horizontalGap * static_cast<double>(grid.columns - 1);
    const auto totalVerticalGap = verticalGap * static_cast<double>(grid.rows - 1);
    const auto cellWidth = std::max(1.0, (regionBox.w - totalHorizontalGap) / static_cast<double>(grid.columns));
    const auto cellHeight = std::max(1.0, (regionBox.h - totalVerticalGap) / static_cast<double>(grid.rows));

    // Cells are emitted row-major so neighboring workspace ids stay spatially
    // close when the sorted id list increases by one.
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
