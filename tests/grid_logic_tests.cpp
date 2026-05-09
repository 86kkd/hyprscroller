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

void test_grid_reports_directional_edges() {
    ScrollerGrid::GridModel model;
    const auto profile = ScrollerGrid::profile_for_workarea(Mode::Row, {0.0, 0.0, 1200.0, 800.0});

    model.add_window(1, profile);
    model.add_window(2, profile);
    model.focus_window(2);

    expect_true(model.has_focus_candidate(Direction::Left), "grid sees a focus candidate toward occupied cells");
    expect_true(!model.active_item_at_edge(Direction::Left), "grid is not at an occupied-cell edge when a candidate exists");
    expect_true(model.active_item_at_edge(Direction::Right), "grid reports an edge when no candidate exists");
}

void test_resize_align_and_page_move() {
    ScrollerGrid::GridModel model;
    const auto profile = ScrollerGrid::profile_for_workarea(Mode::Row, {0.0, 0.0, 1200.0, 800.0});
    ScrollerGrid::GridViewport viewport;

    model.add_window(1, profile);
    expect_eq(model.resize_active_item(1, profile, viewport), ScrollerGrid::GridMoveResult::Moved,
              "grid resize grows active item by one visible unit");
    expect_eq(model.item_for_key(1)->columnSpan, 2, "grid resize updates row span along columns");

    expect_eq(model.align_active(Direction::Right, profile, viewport), ScrollerGrid::GridMoveResult::Moved,
              "grid align can pin active item to viewport end");
    expect_eq(viewport.originColumn, 0, "full-width active item aligns to viewport origin");

    expect_eq(model.move_active_window_to_page(Direction::Right, profile, viewport), ScrollerGrid::GridMoveResult::Moved,
              "grid page move sends active item by one viewport page");
    expect_eq(model.item_for_key(1)->column, 2, "grid page move advances by two half-screen cells");
    expect_eq(viewport.originColumn, 2, "grid viewport follows page-moved active item");
}

void test_grid_snapshot_restore_filters_missing_items() {
    ScrollerGrid::GridModel model;
    const auto profile = ScrollerGrid::profile_for_workarea(Mode::Row, {0.0, 0.0, 1200.0, 800.0});
    ScrollerGrid::GridViewport viewport{.originColumn = 1, .originRow = 0};

    model.add_window(1, profile);
    model.add_window(2, profile);
    const auto snapshot = model.capture_snapshot(viewport);

    ScrollerSnapshot::GridSnapshot filtered = snapshot;
    filtered.items.erase(filtered.items.begin());
    filtered.activeItemIndex = 0;

    ScrollerGrid::GridModel restored;
    restored.restore_snapshot(filtered);
    expect_true(!restored.contains(1), "grid restore can omit stale snapshot items");
    expect_true(restored.contains(2), "grid restore keeps live snapshot item");
    expect_eq(restored.active_item()->key, static_cast<uintptr_t>(2), "grid restore normalizes active item");
}

void test_legacy_snapshot_migrates_to_grid_coordinates() {
    ScrollerSnapshot::CanvasSnapshot snapshot;
    snapshot.workspaceId = 9;
    snapshot.activeLaneIndex = 1;

    ScrollerSnapshot::LaneSnapshot firstLane;
    ScrollerSnapshot::StackSnapshot firstStack;
    firstStack.geom = {0.0, 0.0, 600.0, 800.0};
    firstStack.windows.push_back({.key = 0x11});
    firstLane.stacks.push_back(firstStack);
    snapshot.lanes.push_back(firstLane);

    ScrollerSnapshot::LaneSnapshot secondLane;
    secondLane.activeStackIndex = 0;
    ScrollerSnapshot::StackSnapshot secondStack;
    secondStack.geom = {0.0, 0.0, 600.0, 800.0};
    secondStack.fullscreened = true;
    secondStack.maximized = true;
    secondStack.activeWindowKey = 0x22;
    secondStack.windows.push_back({.key = 0x22});
    secondLane.stacks.push_back(secondStack);
    snapshot.lanes.push_back(secondLane);

    const auto profile = ScrollerGrid::profile_for_workarea(Mode::Row, {0.0, 0.0, 1200.0, 800.0});
    const auto migrated = ScrollerGrid::migrate_legacy_snapshot_to_grid(snapshot, profile);

    expect_true(migrated.enabled, "legacy snapshot migration enables grid snapshot");
    expect_eq(migrated.mode, static_cast<int>(Mode::Row), "legacy snapshot migration stores grid mode");
    expect_eq(migrated.items.size(), static_cast<size_t>(2), "legacy snapshot migration keeps windows");
    expect_eq(migrated.items[0].columnSpan, 1, "legacy half-width stack migrates to one grid cell");
    expect_eq(migrated.items[1].columnSpan, 2, "legacy maximized stack migrates to full grid page");
    expect_eq(migrated.items[1].row, 1, "legacy second lane migrates to next grid page row");
    expect_eq(migrated.activeItemIndex, 1, "legacy snapshot migration keeps active window");
    expect_eq(migrated.fullscreenKey, static_cast<uintptr_t>(0x22), "legacy snapshot migration keeps fullscreen window");
    expect_eq(migrated.viewportRow, 1, "legacy snapshot migration scrolls viewport to active lane");
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
    test_grid_reports_directional_edges();
    test_resize_align_and_page_move();
    test_grid_snapshot_restore_filters_missing_items();
    test_legacy_snapshot_migrates_to_grid_coordinates();
    test_hidden_boxes_keep_waybar_reserved_gap();
}
