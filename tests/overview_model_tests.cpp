#include "overview/model/workspace_grid.h"

#include "test_suite.h"
#include "test_support.h"

namespace {

void test_choose_workspace_grid_shape_prefers_wide_columns() {
    const auto grid = Overview::chooseWorkspaceGridShape({0.0, 0.0, 600.0, 300.0}, 4);
    expect_eq(grid.columns, static_cast<size_t>(3), "wide region chooses more columns");
    expect_eq(grid.rows, static_cast<size_t>(2), "wide region chooses fewer rows");
}

void test_layout_workspace_grid_cells_sorts_and_assigns_boxes() {
    const auto cells = Overview::layoutWorkspaceGridCells({0.0, 0.0, 640.0, 360.0}, {5, 2});
    expect_eq(cells.size(), static_cast<size_t>(2), "grid layout returns one cell per workspace");
    expect_eq(cells.front().workspaceId, 2, "grid layout sorts workspace ids");
    expect_true(cells[0].box.w > 0.0 && cells[0].box.h > 0.0, "grid layout assigns non-empty box dimensions");
    expect_true(cells[1].box.x > cells[0].box.x, "grid layout places later workspaces in later horizontal cells");
}

void test_layout_workspace_grid_cells_stay_inside_small_regions() {
    const ScrollerCore::Box region{0.0, 0.0, 320.0, 180.0};
    const auto cells = Overview::layoutWorkspaceGridCells(region, {8, 6, 4, 2, 7, 5, 3, 1});
    expect_eq(cells.size(), static_cast<size_t>(8), "small-region grid keeps every workspace");
    for (const auto& cell : cells) {
        expect_true(cell.box.x >= region.x && cell.box.y >= region.y,
                    "small-region grid cells keep their origin inside the monitor region");
        expect_true(cell.box.x + cell.box.w <= region.x + region.w + 1e-9,
                    "small-region grid cells stay within region width");
        expect_true(cell.box.y + cell.box.h <= region.y + region.h + 1e-9,
                    "small-region grid cells stay within region height");
    }
}

} // namespace

void run_overview_model_tests() {
    test_choose_workspace_grid_shape_prefers_wide_columns();
    test_layout_workspace_grid_cells_sorts_and_assigns_boxes();
    test_layout_workspace_grid_cells_stay_inside_small_regions();
}
