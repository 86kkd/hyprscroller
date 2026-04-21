#include "overview/workspace_grid.h"

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

} // namespace

void run_overview_model_tests() {
    test_choose_workspace_grid_shape_prefers_wide_columns();
    test_layout_workspace_grid_cells_sorts_and_assigns_boxes();
}
