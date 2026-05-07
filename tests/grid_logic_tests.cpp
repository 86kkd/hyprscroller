#include "layout/grid/grid.h"

#include <string>

#include "test_suite.h"
#include "test_support.h"

namespace {

void expect_box_near(const ScrollerCore::Box& actual,
                     const ScrollerCore::Box& expected,
                     std::string_view label) {
    expect_near(actual.x, expected.x, 1e-9, std::string(label) + " x");
    expect_near(actual.y, expected.y, 1e-9, std::string(label) + " y");
    expect_near(actual.w, expected.w, 1e-9, std::string(label) + " width");
    expect_near(actual.h, expected.h, 1e-9, std::string(label) + " height");
}

void test_grid_profiles_use_half_screen_units() {
    const ScrollerCore::Box landscape{0.0, 40.0, 1920.0, 1040.0};
    const auto row = ScrollerGrid::profile_for_workarea(Mode::Row, landscape);
    expect_eq(row.visibleColumns, 2, "row grid exposes two horizontal units");
    expect_eq(row.visibleRows, 1, "row grid exposes one vertical unit");
    expect_near(row.unitWidth, 960.0, 1e-9, "row grid unit is half workarea width");
    expect_near(row.unitHeight, 1040.0, 1e-9, "row grid unit keeps workarea height");

    const ScrollerCore::Box portrait{0.0, 40.0, 1080.0, 1880.0};
    const auto column = ScrollerGrid::profile_for_workarea(Mode::Column, portrait);
    expect_eq(column.visibleColumns, 1, "column grid exposes one horizontal unit");
    expect_eq(column.visibleRows, 2, "column grid exposes two vertical units");
    expect_near(column.unitWidth, 1080.0, 1e-9, "column grid unit keeps workarea width");
    expect_near(column.unitHeight, 940.0, 1e-9, "column grid unit is half workarea height");
}

void test_grid_inserts_along_orientation_axis() {
    ScrollerGrid::GridModel model;
    const auto row = ScrollerGrid::profile_for_workarea(Mode::Row, {0.0, 0.0, 1200.0, 800.0});
    expect_true(model.add_window(1, row), "grid inserts first row item");
    expect_true(model.add_window(2, row), "grid inserts second row item");
    expect_true(model.add_window(3, row), "grid inserts third row item");

    const auto* first = model.item_for_key(1);
    const auto* second = model.item_for_key(2);
    const auto* third = model.item_for_key(3);
    expect_true(first && second && third, "grid stores inserted row items");
    expect_eq(first->column, 0, "first row item starts at column zero");
    expect_eq(second->column, 1, "second row item advances one column");
    expect_eq(third->column, 2, "third row item advances another column");

    ScrollerGrid::GridModel portraitModel;
    const auto column = ScrollerGrid::profile_for_workarea(Mode::Column, {0.0, 0.0, 800.0, 1200.0});
    expect_true(portraitModel.add_window(10, column), "grid inserts first column item");
    expect_true(portraitModel.add_window(11, column), "grid inserts second column item");
    const auto* portraitSecond = portraitModel.item_for_key(11);
    expect_true(portraitSecond != nullptr, "grid stores second column item");
    expect_eq(portraitSecond->column, 0, "column grid keeps same column");
    expect_eq(portraitSecond->row, 1, "column grid advances rows");
}

void test_move_focus_scrolls_viewport_to_active_item() {
    ScrollerGrid::GridModel model;
    const auto profile = ScrollerGrid::profile_for_workarea(Mode::Row, {0.0, 0.0, 1200.0, 800.0});
    ScrollerGrid::GridViewport viewport;

    model.add_window(1, profile);
    model.add_window(2, profile);
    model.add_window(3, profile);
    model.focus_window(1);

    expect_eq(model.move_focus(Direction::Right, profile, viewport, false), ScrollerGrid::GridMoveResult::Moved,
              "grid focus moves right to visible neighbor");
    expect_eq(model.active_item()->key, static_cast<uintptr_t>(2), "active item becomes second window");
    expect_eq(viewport.originColumn, 0, "viewport remains stable while active stays visible");

    expect_eq(model.move_focus(Direction::Right, profile, viewport, false), ScrollerGrid::GridMoveResult::Moved,
              "grid focus moves right to offscreen neighbor");
    expect_eq(model.active_item()->key, static_cast<uintptr_t>(3), "active item becomes third window");
    expect_eq(viewport.originColumn, 1, "viewport scrolls by one half-screen unit");
}

void test_move_focus_can_move_empty_viewport_space() {
    ScrollerGrid::GridModel model;
    const auto profile = ScrollerGrid::profile_for_workarea(Mode::Row, {0.0, 0.0, 1200.0, 800.0});
    ScrollerGrid::GridViewport viewport;

    model.add_window(1, profile);
    expect_eq(model.move_focus(Direction::Right, profile, viewport, false), ScrollerGrid::GridMoveResult::Moved,
              "grid viewport can move into empty space");
    expect_eq(viewport.originColumn, 1, "viewport moves even without a focus candidate");
    expect_eq(model.active_item()->key, static_cast<uintptr_t>(1), "active item is preserved while viewport moves");
}

void test_move_active_window_moves_or_swaps_grid_cells() {
    ScrollerGrid::GridModel model;
    const auto profile = ScrollerGrid::profile_for_workarea(Mode::Row, {0.0, 0.0, 1200.0, 800.0});
    ScrollerGrid::GridViewport viewport;

    model.add_window(1, profile);
    model.add_window(2, profile);
    model.focus_window(2);

    expect_eq(model.move_active_window(Direction::Right, profile, viewport), ScrollerGrid::GridMoveResult::Moved,
              "grid movewindow moves into empty cell");
    expect_eq(model.item_for_key(2)->column, 2, "moved item advances to empty cell");
    expect_eq(viewport.originColumn, 1, "viewport follows moved item");

    expect_eq(model.move_active_window(Direction::Left, profile, viewport), ScrollerGrid::GridMoveResult::Moved,
              "grid movewindow moves back into empty cell");
    expect_eq(model.move_active_window(Direction::Left, profile, viewport), ScrollerGrid::GridMoveResult::Moved,
              "grid movewindow swaps with occupied cell");
    expect_eq(model.item_for_key(2)->column, 0, "active item takes occupied cell");
    expect_eq(model.item_for_key(1)->column, 1, "neighbor moves into active item's previous cell");
}

void test_hidden_boxes_keep_waybar_reserved_gap() {
    const ScrollerCore::Box full{0.0, 0.0, 1080.0, 1920.0};
    const ScrollerCore::Box workarea{0.0, 40.0, 1080.0, 1880.0};
    const auto profile = ScrollerGrid::profile_for_workarea(Mode::Column, workarea);
    const ScrollerGrid::GridViewport viewport;
    const ScrollerGrid::GridItem above{.key = 7, .column = 0, .row = -1};

    const auto rendered = ScrollerGrid::render_grid_item(above, viewport, profile, full, workarea);
    expect_box_near(rendered.logicalBox, {0.0, -900.0, 1080.0, 940.0}, "logical hidden top box");
    expect_box_near(rendered.committedBox, {0.0, -940.0, 1080.0, 940.0}, "committed hidden top box");
    expect_true(!rendered.visible, "hidden top item is not visible");

    const ScrollerCore::Box fullWithLeftBar{0.0, 0.0, 1920.0, 1080.0};
    const ScrollerCore::Box workareaWithLeftBar{48.0, 0.0, 1872.0, 1080.0};
    const auto rowProfile = ScrollerGrid::profile_for_workarea(Mode::Row, workareaWithLeftBar);
    const ScrollerGrid::GridItem left{.key = 8, .column = -1, .row = 0};
    const auto leftRendered = ScrollerGrid::render_grid_item(left, viewport, rowProfile, fullWithLeftBar, workareaWithLeftBar);
    expect_box_near(leftRendered.logicalBox, {-888.0, 0.0, 936.0, 1080.0}, "logical hidden left box");
    expect_box_near(leftRendered.committedBox, {-936.0, 0.0, 936.0, 1080.0}, "committed hidden left box");
}

} // namespace

void run_grid_logic_tests() {
    test_grid_profiles_use_half_screen_units();
    test_grid_inserts_along_orientation_axis();
    test_move_focus_scrolls_viewport_to_active_item();
    test_move_focus_can_move_empty_viewport_space();
    test_move_active_window_moves_or_swaps_grid_cells();
    test_hidden_boxes_keep_waybar_reserved_gap();
}
