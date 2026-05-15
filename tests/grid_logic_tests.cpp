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
    const auto* rowFirstOnly = model.item_for_key(1);
    expect_true(rowFirstOnly != nullptr, "grid stores first row item");
    expect_eq(rowFirstOnly->columnSpan, 2, "first row item fills visible page width");
    expect_eq(rowFirstOnly->rowSpan, 1, "first row item keeps visible page height");

    expect_true(model.add_window(2, row), "grid inserts second row item");
    expect_eq(model.item_for_key(1)->columnSpan, 1, "second row item shrinks first row item to one unit");
    expect_eq(model.item_for_key(1)->rowSpan, 1, "second row item keeps first row item one unit tall");

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
    const auto* portraitFirstOnly = portraitModel.item_for_key(10);
    expect_true(portraitFirstOnly != nullptr, "grid stores first column item");
    expect_eq(portraitFirstOnly->columnSpan, 1, "first column item keeps visible page width");
    expect_eq(portraitFirstOnly->rowSpan, 2, "first column item fills visible page height");

    expect_true(portraitModel.add_window(11, column), "grid inserts second column item");
    expect_eq(portraitModel.item_for_key(10)->columnSpan, 1, "second column item keeps first column item one unit wide");
    expect_eq(portraitModel.item_for_key(10)->rowSpan, 1, "second column item shrinks first column item to one unit");

    const auto* portraitSecond = portraitModel.item_for_key(11);
    expect_true(portraitSecond != nullptr, "grid stores second column item");
    expect_eq(portraitSecond->column, 0, "column grid keeps same column");
    expect_eq(portraitSecond->row, 1, "column grid advances rows");
}

void test_grid_restores_single_remaining_item_to_full_page() {
    ScrollerGrid::GridModel model;
    const auto row = ScrollerGrid::profile_for_workarea(Mode::Row, {0.0, 0.0, 1200.0, 800.0});
    ScrollerGrid::GridViewport viewport;

    model.add_window(1, row);
    model.add_window(2, row);
    expect_eq(model.item_for_key(1)->columnSpan, 1, "second row window shrinks first row item before removal");
    expect_true(model.remove_window(2), "grid removes second row item");
    model.expand_single_item_to_page(row, viewport);
    expect_eq(model.item_for_key(1)->columnSpan, 2, "remaining row item restores full page width");
    expect_eq(model.item_for_key(1)->rowSpan, 1, "remaining row item keeps full page height");
    expect_eq(viewport.originColumn, 0, "row viewport returns to full-page origin");

    ScrollerGrid::GridModel portraitModel;
    const auto column = ScrollerGrid::profile_for_workarea(Mode::Column, {0.0, 0.0, 800.0, 1200.0});
    ScrollerGrid::GridViewport portraitViewport;

    portraitModel.add_window(10, column);
    portraitModel.add_window(11, column);
    expect_eq(portraitModel.item_for_key(10)->rowSpan, 1, "second column window shrinks first column item before removal");
    expect_true(portraitModel.remove_window(11), "grid removes second column item");
    portraitModel.expand_single_item_to_page(column, portraitViewport);
    expect_eq(portraitModel.item_for_key(10)->columnSpan, 1, "remaining column item keeps full page width");
    expect_eq(portraitModel.item_for_key(10)->rowSpan, 2, "remaining column item restores full page height");
    expect_eq(portraitViewport.originRow, 0, "column viewport returns to full-page origin");
}

void test_grid_settles_viewport_to_remaining_items_after_removal() {
    ScrollerGrid::GridModel model;
    const auto row = ScrollerGrid::profile_for_workarea(Mode::Row, {0.0, 0.0, 1200.0, 800.0});
    ScrollerGrid::GridViewport viewport{.originColumn = 1, .originRow = 0};

    model.add_window(1, row);
    model.add_window(2, row);
    model.add_window(3, row);
    expect_true(model.remove_window(3), "grid removes trailing row item");
    model.settle_after_removal(row, viewport);
    expect_eq(viewport.originColumn, 0, "row viewport shifts back to show both remaining windows");
    expect_eq(model.item_for_key(1)->columnSpan, 1, "row settle keeps first remaining item one unit wide");
    expect_eq(model.item_for_key(2)->columnSpan, 1, "row settle keeps second remaining item one unit wide");

    ScrollerGrid::GridModel portraitModel;
    const auto column = ScrollerGrid::profile_for_workarea(Mode::Column, {0.0, 0.0, 800.0, 1200.0});
    ScrollerGrid::GridViewport portraitViewport{.originColumn = 0, .originRow = 1};

    portraitModel.add_window(10, column);
    portraitModel.add_window(11, column);
    portraitModel.add_window(12, column);
    expect_true(portraitModel.remove_window(12), "grid removes trailing column item");
    portraitModel.settle_after_removal(column, portraitViewport);
    expect_eq(portraitViewport.originRow, 0, "column viewport shifts back to show both remaining windows");
    expect_eq(portraitModel.item_for_key(10)->rowSpan, 1, "column settle keeps first remaining item one unit tall");
    expect_eq(portraitModel.item_for_key(11)->rowSpan, 1, "column settle keeps second remaining item one unit tall");
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
              "grid resize cycles full-page active item to one visible unit");
    expect_eq(model.item_for_key(1)->columnSpan, 1, "grid resize wraps row span along columns");

    expect_eq(model.align_active(Direction::Right, profile, viewport), ScrollerGrid::GridMoveResult::Moved,
              "grid align can pin active item to viewport end");
    expect_eq(viewport.originColumn, -1, "one-unit active item aligns to viewport end");

    expect_eq(model.move_active_window_to_page(Direction::Right, profile, viewport), ScrollerGrid::GridMoveResult::Moved,
              "grid page move sends active item by one viewport page");
    expect_eq(model.item_for_key(1)->column, 2, "grid page move advances by two half-screen cells");
    expect_eq(viewport.originColumn, 1, "grid viewport keeps page-moved active item visible");
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

void test_grid_render_applies_inner_gaps() {
    const ScrollerCore::Box landscape{0.0, 0.0, 1200.0, 800.0};
    const auto rowProfile = ScrollerGrid::profile_for_workarea(Mode::Row, landscape);
    ScrollerGrid::GridViewport viewport;
    ScrollerGrid::GridModel rowModel;
    rowModel.add_window(1, rowProfile);
    rowModel.add_window(2, rowProfile);

    const auto rowRendered = rowModel.render(viewport, rowProfile, landscape, landscape, 8.0);
    expect_eq(rowRendered.size(), static_cast<size_t>(2), "row grid renders both windows with gaps");
    expect_box_near(rowRendered[0].committedBox, {0.0, 0.0, 592.0, 800.0}, "row first item keeps left page edge");
    expect_box_near(rowRendered[1].committedBox, {608.0, 0.0, 592.0, 800.0}, "row second item keeps right page edge");

    ScrollerGrid::GridModel singleRowModel;
    singleRowModel.add_window(3, rowProfile);
    const auto singleRendered = singleRowModel.render(viewport, rowProfile, landscape, landscape, 8.0);
    expect_box_near(singleRendered[0].committedBox, landscape, "single full-page row item keeps full workarea");

    const ScrollerCore::Box portrait{0.0, 0.0, 800.0, 1200.0};
    const auto columnProfile = ScrollerGrid::profile_for_workarea(Mode::Column, portrait);
    ScrollerGrid::GridModel columnModel;
    columnModel.add_window(10, columnProfile);
    columnModel.add_window(11, columnProfile);

    const auto columnRendered = columnModel.render(viewport, columnProfile, portrait, portrait, 8.0);
    expect_eq(columnRendered.size(), static_cast<size_t>(2), "column grid renders both windows with gaps");
    expect_box_near(columnRendered[0].committedBox, {0.0, 0.0, 800.0, 592.0}, "column first item keeps top page edge");
    expect_box_near(columnRendered[1].committedBox, {0.0, 608.0, 800.0, 592.0}, "column second item keeps bottom page edge");
}

} // namespace

void run_grid_logic_tests() {
    test_grid_profiles_use_half_screen_units();
    test_grid_inserts_along_orientation_axis();
    test_grid_restores_single_remaining_item_to_full_page();
    test_grid_settles_viewport_to_remaining_items_after_removal();
    test_move_focus_scrolls_viewport_to_active_item();
    test_move_focus_can_move_empty_viewport_space();
    test_move_active_window_moves_or_swaps_grid_cells();
    test_grid_reports_directional_edges();
    test_resize_align_and_page_move();
    test_grid_snapshot_restore_filters_missing_items();
    test_legacy_snapshot_migrates_to_grid_coordinates();
    test_hidden_boxes_keep_waybar_reserved_gap();
    test_grid_render_applies_inner_gaps();
}
