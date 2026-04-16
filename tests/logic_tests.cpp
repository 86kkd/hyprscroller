#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include "core/direction.h"
#include "core/fit_size.h"
#include "core/interval.h"
#include "core/layout_math.h"
#include "core/monitor_geometry.h"
#include "core/owner_index.h"
#include "core/layout_profile.h"
#include "list.h"
#include "overview/logic.h"
#include "overview/orientation_math.h"
#include "layout/canvas/dispatch_logic.h"
#include "layout/canvas/handoff_state.h"
#include "layout/canvas/route_logic.h"

namespace {

int failures = 0;

struct FakeDispatcherRuntime final : CanvasLayoutInternal::DispatcherRegistryRuntime {
    bool registryAvailable = true;
    bool invocationSucceeds = true;
    std::vector<std::string> knownDispatchers;
    mutable std::vector<std::pair<std::string, std::string>> invocations;

    bool hasDispatcherRegistry() const override {
        return registryAvailable;
    }

    bool hasDispatcher(const char *dispatcher) const override {
        if (!dispatcher)
            return false;

        for (const auto &candidate : knownDispatchers) {
            if (candidate == dispatcher)
                return true;
        }

        return false;
    }

    bool invokeDispatcher(const char *dispatcher, std::string_view arg) const override {
        if (!hasDispatcher(dispatcher) || !invocationSucceeds)
            return false;

        invocations.emplace_back(dispatcher, std::string(arg));
        return true;
    }
};

struct FakeOwner {
    int              id = 0;
    std::vector<int> keys;

    bool owns(int key) const {
        return std::find(keys.begin(), keys.end(), key) != keys.end();
    }
};

struct FakeFitItem {
    double span = 0.0;
    bool   visible = false;
};

void expect_true(bool condition, std::string_view message) {
    if (condition)
        return;

    std::cerr << "FAIL: " << message << '\n';
    ++failures;
}

template <typename T>
void expect_eq(const T &actual, const T &expected, std::string_view message) {
    if (actual == expected)
        return;

    std::cerr << "FAIL: " << message << '\n';
    ++failures;
}

void expect_near(double actual, double expected, double epsilon, std::string_view message) {
    if (std::abs(actual - expected) <= epsilon)
        return;

    std::cerr << "FAIL: " << message << " actual=" << actual << " expected=" << expected << '\n';
    ++failures;
}

void expect_reserved_workarea(std::string_view label,
                              const Hyprutils::Math::Vector2D &position,
                              const Hyprutils::Math::Vector2D &rawSize,
                              const Hyprutils::Math::Vector2D &transformedSize,
                              wl_output_transform transform,
                              const ScrollerCore::ReservedEdges &rawReserved,
                              const ScrollerCore::ReservedEdges &expectedReserved,
                              const ScrollerCore::Box &expectedWorkarea) {
    const auto logicalReserved = ScrollerCore::logical_reserved_edges(rawSize, transformedSize, transform, rawReserved);
    expect_near(logicalReserved.top, expectedReserved.top, 1e-9, std::string(label) + " reserved top");
    expect_near(logicalReserved.right, expectedReserved.right, 1e-9, std::string(label) + " reserved right");
    expect_near(logicalReserved.bottom, expectedReserved.bottom, 1e-9, std::string(label) + " reserved bottom");
    expect_near(logicalReserved.left, expectedReserved.left, 1e-9, std::string(label) + " reserved left");

    const auto workarea = ScrollerCore::logical_workarea_box(position, rawSize, transformedSize, transform, rawReserved, 0.0);
    expect_near(workarea.x, expectedWorkarea.x, 1e-9, std::string(label) + " workarea x");
    expect_near(workarea.y, expectedWorkarea.y, 1e-9, std::string(label) + " workarea y");
    expect_near(workarea.w, expectedWorkarea.w, 1e-9, std::string(label) + " workarea width");
    expect_near(workarea.h, expectedWorkarea.h, 1e-9, std::string(label) + " workarea height");
}

void test_interval() {
    expect_true(ScrollerCore::Interval::intersects(0.0, 10.0, 5.0, 15.0), "interval partial overlap intersects");
    expect_true(ScrollerCore::Interval::intersects(0.0, 20.0, 5.0, 15.0), "interval containing viewport intersects");
    expect_true(!ScrollerCore::Interval::intersects(0.0, 5.0, 5.0, 15.0), "touching edge does not intersect");
    expect_true(ScrollerCore::Interval::fully_visible(6.0, 9.0, 5.0, 15.0), "fully visible interval reports true");
    expect_true(!ScrollerCore::Interval::fully_visible(4.0, 9.0, 5.0, 15.0), "partially clipped interval reports false");
}

void test_direction_helpers() {
    expect_eq(std::string_view(ScrollerCore::direction_name(Direction::Begin)), std::string_view("begin"),
              "direction_name returns begin");
    expect_eq(std::string_view(ScrollerCore::direction_dispatch_arg(Direction::Right)), std::string_view("r"),
              "direction_dispatch_arg returns short right");
    expect_true(ScrollerCore::direction_dispatch_arg(Direction::Center) == nullptr,
                "direction_dispatch_arg rejects center");
    expect_eq(ScrollerCore::opposite_direction(Direction::Up), Direction::Down,
              "opposite_direction flips up to down");
}

void test_parse_helpers() {
    const auto down = ScrollerCore::parse_direction_arg("dn");
    expect_true(down.has_value() && *down == Direction::Down, "parse_direction_arg handles dn alias");

    const auto center = ScrollerCore::parse_direction_arg("centre");
    expect_true(center.has_value() && *center == Direction::Center, "parse_direction_arg handles centre alias");

    expect_true(!ScrollerCore::parse_direction_arg("sideways").has_value(),
                "parse_direction_arg rejects invalid input");

    const auto toBeginning = ScrollerCore::parse_fit_size_arg("tobeginning");
    expect_true(toBeginning.has_value() && *toBeginning == FitSize::ToBeg,
                "parse_fit_size_arg handles tobeginning");

    expect_true(!ScrollerCore::parse_fit_size_arg("largest").has_value(),
                "parse_fit_size_arg rejects invalid input");

    const auto rowMode = ScrollerCore::parse_mode_arg("row");
    expect_true(rowMode.has_value() && *rowMode == Mode::Row,
                "parse_mode_arg handles row");

    const auto columnMode = ScrollerCore::parse_mode_arg("col");
    expect_true(columnMode.has_value() && *columnMode == Mode::Column,
                "parse_mode_arg handles col alias");

    expect_true(!ScrollerCore::parse_mode_arg("grid").has_value(),
                "parse_mode_arg rejects invalid input");
}

void test_anchor_selection() {
    const ScrollerCore::Box visible(100.0, 50.0, 400.0, 300.0);

    expect_near(ScrollerCore::choose_anchor_x(true, false, 150.0, 120.0, 0.0, 260.0, visible),
                230.0, 1e-9, "choose_anchor_x keeps active and next visible");
    expect_near(ScrollerCore::choose_anchor_x(true, true, 200.0, 250.0, 150.0, 275.0, visible),
                250.0, 1e-9, "choose_anchor_x falls back to prev width when next does not fit");
    expect_near(ScrollerCore::choose_anchor_x(false, true, 350.0, 0.0, 100.0, 275.0, visible),
                150.0, 1e-9, "choose_anchor_x aligns active to right edge when only prev exists");

    expect_near(ScrollerCore::choose_anchor_y(true, false, 120.0, 100.0, 0.0, visible),
                130.0, 1e-9, "choose_anchor_y keeps next visible when possible");
    expect_near(ScrollerCore::choose_anchor_y(false, true, 180.0, 0.0, 100.0, visible),
                150.0, 1e-9, "choose_anchor_y positions after prev when it fits");
    expect_near(ScrollerCore::choose_anchor_y(false, true, 250.0, 0.0, 100.0, visible),
                100.0, 1e-9, "choose_anchor_y aligns to bottom when only prev exists but cannot fit");

    expect_near(ScrollerCore::center_span(100.0, 400.0, 150.0),
                225.0, 1e-9, "center_span preserves the outer origin when centering");
    expect_near(ScrollerCore::center_span(-320.0, 640.0, 320.0),
                -160.0, 1e-9, "center_span handles non-zero negative origins");

    const auto rendered = ScrollerCore::rendered_local_interval(-1855.0, 1908.0, 4.0, 0.0, 16.0);
    expect_near(rendered.start, -1851.0, 1e-9, "rendered_local_interval applies border and leading gap to start");
    expect_near(rendered.end, 33.0, 1e-9, "rendered_local_interval matches final client bottom after border and trailing gap");
}

void test_overview_projection() {
    const ScrollerCore::Box visible(0.0, 0.0, 200.0, 100.0);
    const std::vector<ScrollerCore::OverviewRect> items = {
        {.x0 = 10.0, .x1 = 60.0, .y0 = 20.0, .y1 = 70.0},
        {.x0 = 60.0, .x1 = 110.0, .y0 = 10.0, .y1 = 90.0},
    };

    const auto projection = ScrollerCore::compute_overview_projection(items, visible);
    expect_near(projection.min.x, 10.0, 1e-9, "overview projection tracks minimum x");
    expect_near(projection.min.y, 10.0, 1e-9, "overview projection tracks minimum y");
    expect_near(projection.max.x, 110.0, 1e-9, "overview projection tracks maximum x");
    expect_near(projection.max.y, 90.0, 1e-9, "overview projection tracks maximum y");
    expect_near(projection.width, 100.0, 1e-9, "overview projection width is derived from bounds");
    expect_near(projection.height, 80.0, 1e-9, "overview projection height is derived from bounds");
    expect_near(projection.scale, 1.25, 1e-9, "overview projection chooses the limiting scale");
    expect_near(projection.offset.x, 37.5, 1e-9, "overview projection centers on x");
    expect_near(projection.offset.y, 0.0, 1e-9, "overview projection centers on y");

    const std::vector<ScrollerCore::OverviewRect> degenerate = {
        {.x0 = 50.0, .x1 = 50.0, .y0 = 10.0, .y1 = 40.0},
    };
    const auto degenerateProjection = ScrollerCore::compute_overview_projection(degenerate, visible);
    expect_near(degenerateProjection.scale, 1.0, 1e-9, "degenerate projection keeps scale at 1");
    expect_near(degenerateProjection.offset.x, 50.0, 1e-9, "degenerate projection preserves raw x offset");
    expect_near(degenerateProjection.offset.y, 10.0, 1e-9, "degenerate projection preserves raw y offset");
}

void test_layout_profile() {
    expect_eq(ScrollerCore::layout_orientation_for_extent(1920.0, 1080.0),
              ScrollerCore::LayoutOrientation::Landscape,
              "wide extents map to landscape");
    expect_eq(ScrollerCore::layout_orientation_for_extent(1080.0, 1920.0),
              ScrollerCore::LayoutOrientation::Portrait,
              "tall extents map to portrait");
    expect_eq(ScrollerCore::default_mode_for_extent(1920.0, 1080.0),
              Mode::Row,
              "landscape extents default to row mode");
    expect_eq(ScrollerCore::default_mode_for_extent(1080.0, 1920.0),
              Mode::Column,
              "portrait extents default to column mode");

    expect_true(ScrollerCore::mode_pages_lanes_vertically(Mode::Row),
                "row mode pages lanes vertically");

    expect_eq(ScrollerCore::local_item_backward_direction(Mode::Row),
              Direction::Left,
              "row mode local backward direction is left");
    expect_eq(ScrollerCore::local_item_forward_direction(Mode::Column),
              Direction::Down,
              "column mode local forward direction is down");
    expect_eq(ScrollerCore::stack_item_backward_direction(Mode::Row),
              Direction::Up,
              "row mode stack backward direction is up");
    expect_eq(ScrollerCore::stack_item_forward_direction(Mode::Column),
              Direction::Right,
              "column mode stack forward direction is right");
    expect_eq(ScrollerCore::lane_backward_direction(Mode::Row),
              Direction::Up,
              "row mode lane backward direction is up");
    expect_eq(ScrollerCore::lane_forward_direction(Mode::Column),
              Direction::Right,
              "column mode lane forward direction is right");

    expect_near(ScrollerCore::stack_local_origin_for_window(Mode::Row, {10.0, 20.0}),
                20.0, 1e-9, "row mode stack-local window origin uses y");
    expect_near(ScrollerCore::stack_local_origin_for_window(Mode::Column, {10.0, 20.0}),
                10.0, 1e-9, "column mode stack-local window origin uses x");
    expect_near(ScrollerCore::stack_primary_span_limit(Mode::Row, {100.0, 50.0, 640.0, 360.0}),
                640.0, 1e-9, "row mode stack primary span limit uses workarea width");
    expect_near(ScrollerCore::stack_primary_span_limit(Mode::Column, {100.0, 50.0, 640.0, 360.0}),
                360.0, 1e-9, "column mode stack primary span limit uses workarea height");

    expect_true(ScrollerCore::direction_targets_local_item(Mode::Row, Direction::Left),
                "row mode treats left as local item movement");
    expect_true(ScrollerCore::direction_moves_between_lanes(Mode::Row, Direction::Down),
                "row mode treats down as cross-lane movement");
    expect_true(ScrollerCore::direction_moves_between_lanes(Mode::Column, Direction::Left),
                "column mode treats left as cross-lane movement");
    expect_true(ScrollerCore::direction_inserts_before_current(Mode::Column, Direction::Left),
                "column mode inserts before current on left");

    const auto portraitPrediction = ScrollerCore::predict_window_size(Mode::Column, {0.0, 0.0, 800.0, 600.0});
    expect_near(portraitPrediction.x, 800.0, 1e-9, "column mode prediction keeps full width");
    expect_near(portraitPrediction.y, 300.0, 1e-9, "column mode prediction halves height");
}

void test_monitor_geometry() {
    const Hyprutils::Math::Vector2D landscapePosition{3840.0, 0.0};
    const Hyprutils::Math::Vector2D landscapeRawSize{3840.0, 2160.0};
    const Hyprutils::Math::Vector2D landscapeTransformedSize{3840.0, 2160.0};
    const Hyprutils::Math::Vector2D portraitPosition{1680.0, 0.0};
    const Hyprutils::Math::Vector2D portraitRawSize{3840.0, 2160.0};
    const Hyprutils::Math::Vector2D portraitTransformedSize{2160.0, 3840.0};

    expect_reserved_workarea("landscape top",
                             landscapePosition,
                             landscapeRawSize,
                             landscapeTransformedSize,
                             WL_OUTPUT_TRANSFORM_NORMAL,
                             {.top = 37.0, .right = 0.0, .bottom = 0.0, .left = 0.0},
                             {.top = 37.0, .right = 0.0, .bottom = 0.0, .left = 0.0},
                             {3840.0, 37.0, 3840.0, 2123.0});
    expect_reserved_workarea("landscape right",
                             landscapePosition,
                             landscapeRawSize,
                             landscapeTransformedSize,
                             WL_OUTPUT_TRANSFORM_NORMAL,
                             {.top = 0.0, .right = 37.0, .bottom = 0.0, .left = 0.0},
                             {.top = 0.0, .right = 37.0, .bottom = 0.0, .left = 0.0},
                             {3840.0, 0.0, 3803.0, 2160.0});
    expect_reserved_workarea("landscape bottom",
                             landscapePosition,
                             landscapeRawSize,
                             landscapeTransformedSize,
                             WL_OUTPUT_TRANSFORM_NORMAL,
                             {.top = 0.0, .right = 0.0, .bottom = 37.0, .left = 0.0},
                             {.top = 0.0, .right = 0.0, .bottom = 37.0, .left = 0.0},
                             {3840.0, 0.0, 3840.0, 2123.0});
    expect_reserved_workarea("landscape left",
                             landscapePosition,
                             landscapeRawSize,
                             landscapeTransformedSize,
                             WL_OUTPUT_TRANSFORM_NORMAL,
                             {.top = 0.0, .right = 0.0, .bottom = 0.0, .left = 37.0},
                             {.top = 0.0, .right = 0.0, .bottom = 0.0, .left = 37.0},
                             {3877.0, 0.0, 3803.0, 2160.0});

    const auto portraitSize = ScrollerCore::logical_monitor_size({3840.0, 2160.0},
                                                                 {2160.0, 3840.0},
                                                                 WL_OUTPUT_TRANSFORM_270);
    expect_near(portraitSize.x, 2160.0, 1e-9, "portrait logical width uses transformed width");
    expect_near(portraitSize.y, 3840.0, 1e-9, "portrait logical height uses transformed height");

    expect_reserved_workarea("portrait top",
                             portraitPosition,
                             portraitRawSize,
                             portraitTransformedSize,
                             WL_OUTPUT_TRANSFORM_270,
                             {.top = 37.0, .right = 0.0, .bottom = 0.0, .left = 0.0},
                             {.top = 37.0, .right = 0.0, .bottom = 0.0, .left = 0.0},
                             {1680.0, 37.0, 2160.0, 3803.0});
    expect_reserved_workarea("portrait right",
                             portraitPosition,
                             portraitRawSize,
                             portraitTransformedSize,
                             WL_OUTPUT_TRANSFORM_270,
                             {.top = 0.0, .right = 37.0, .bottom = 0.0, .left = 0.0},
                             {.top = 0.0, .right = 37.0, .bottom = 0.0, .left = 0.0},
                             {1680.0, 0.0, 2123.0, 3840.0});
    expect_reserved_workarea("portrait bottom",
                             portraitPosition,
                             portraitRawSize,
                             portraitTransformedSize,
                             WL_OUTPUT_TRANSFORM_270,
                             {.top = 0.0, .right = 0.0, .bottom = 37.0, .left = 0.0},
                             {.top = 0.0, .right = 0.0, .bottom = 37.0, .left = 0.0},
                             {1680.0, 0.0, 2160.0, 3803.0});
    expect_reserved_workarea("portrait left",
                             portraitPosition,
                             portraitRawSize,
                             portraitTransformedSize,
                             WL_OUTPUT_TRANSFORM_270,
                             {.top = 0.0, .right = 0.0, .bottom = 0.0, .left = 37.0},
                             {.top = 0.0, .right = 0.0, .bottom = 0.0, .left = 37.0},
                             {1717.0, 0.0, 2123.0, 3840.0});

    const auto fallbackSize = ScrollerCore::logical_monitor_size({3840.0, 2160.0},
                                                                 {0.0, 0.0},
                                                                 WL_OUTPUT_TRANSFORM_270);
    expect_near(fallbackSize.x, 2160.0, 1e-9, "missing transformed size still swaps portrait axes");
    expect_near(fallbackSize.y, 3840.0, 1e-9, "missing transformed size still preserves portrait height");
}

void test_monitor_space_orientation() {
    using Overview::MonitorOrientation;

    expect_eq(Overview::orientation_for_transform(WL_OUTPUT_TRANSFORM_NORMAL),
              MonitorOrientation::Landscape,
              "normal transform is landscape");
    expect_eq(Overview::orientation_for_transform(WL_OUTPUT_TRANSFORM_180),
              MonitorOrientation::Landscape,
              "180 transform stays landscape");
    expect_eq(Overview::orientation_for_transform(WL_OUTPUT_TRANSFORM_90),
              MonitorOrientation::Portrait,
              "90 transform is portrait");
    expect_eq(Overview::orientation_for_transform(WL_OUTPUT_TRANSFORM_270),
              MonitorOrientation::Portrait,
              "270 transform is portrait");

    const auto portraitRenderBox = Overview::transform_box_to_render_space({10.0, 20.0, 100.0, 200.0},
                                                                           WL_OUTPUT_TRANSFORM_270,
                                                                           1080.0,
                                                                           1920.0);
    expect_near(portraitRenderBox.x, 860.0, 1e-9, "portrait transform remaps x into render space");
    expect_near(portraitRenderBox.y, 10.0, 1e-9, "portrait transform remaps y into render space");
    expect_near(portraitRenderBox.w, 200.0, 1e-9, "portrait transform swaps width");
    expect_near(portraitRenderBox.h, 100.0, 1e-9, "portrait transform swaps height");

    const auto landscapeRenderBox = Overview::transform_box_to_render_space({10.0, 20.0, 100.0, 200.0},
                                                                            WL_OUTPUT_TRANSFORM_NORMAL,
                                                                            1920.0,
                                                                            1080.0);
    expect_near(landscapeRenderBox.x, 10.0, 1e-9, "landscape transform keeps x stable");
    expect_near(landscapeRenderBox.y, 20.0, 1e-9, "landscape transform keeps y stable");
    expect_near(landscapeRenderBox.w, 100.0, 1e-9, "landscape transform keeps width stable");
    expect_near(landscapeRenderBox.h, 200.0, 1e-9, "landscape transform keeps height stable");
}

void test_handoff_state() {
    HandoffState state;

    expect_eq(state.consumeActiveLaneSyncPolicy(), ActiveLaneSyncPolicy::WorkspaceFocus,
              "handoff state defaults to workspace focus sync");

    state.requestWorkspaceFocusSyncSuppression();
    expect_eq(state.consumeActiveLaneSyncPolicy(), ActiveLaneSyncPolicy::None,
              "handoff state consumes focus suppression once");
    expect_eq(state.consumeActiveLaneSyncPolicy(), ActiveLaneSyncPolicy::WorkspaceFocus,
              "handoff state resets focus suppression after consume");

    state.rememberManualCrossMonitorInsertion(0x42);
    expect_true(state.hasPendingManualCrossMonitorInsertion(0x42),
                "handoff state tracks manual cross-monitor insertion keys");
    state.forgetManualCrossMonitorInsertion(0x42);
    expect_true(!state.hasPendingManualCrossMonitorInsertion(0x42),
                "handoff state clears manual cross-monitor insertion keys");

    state.requestWorkspaceFocusSyncSuppression();
    state.rememberManualCrossMonitorInsertion(0x99);
    state.reset();
    expect_eq(state.consumeActiveLaneSyncPolicy(), ActiveLaneSyncPolicy::WorkspaceFocus,
              "handoff state reset restores default sync policy");
    expect_true(!state.hasPendingManualCrossMonitorInsertion(0x99),
                "handoff state reset clears pending insertion keys");
}

void test_route_logic() {
    using namespace CanvasLayoutInternal;

    expect_true(!direction_moves_between_lanes(Mode::Row, Direction::Left),
                "row left stays inside the current lane");
    expect_true(direction_moves_between_lanes(Mode::Row, Direction::Up),
                "row up moves between lanes");
    expect_true(direction_inserts_before_current(Mode::Column, Direction::Left),
                "column left inserts before current lane");

    expect_eq(choose_directional_handoff_route(false, true, true, true),
              DirectionalHandoffRoute::NoOp,
              "same-lane movement never chooses a cross-lane handoff");
    expect_eq(choose_directional_handoff_route(true, true, false, true),
              DirectionalHandoffRoute::AdjacentLane,
              "adjacent lane route wins before create or cross-monitor");
    expect_eq(choose_directional_handoff_route(true, false, true, true),
              DirectionalHandoffRoute::CrossMonitor,
              "cross-monitor route wins when no adjacent lane exists");
    expect_eq(choose_directional_handoff_route(true, false, false, true),
              DirectionalHandoffRoute::CreateLane,
              "missing adjacent lane and monitor falls back to lane creation");

    expect_eq(decide_move_focus_route(true, false, false, FocusMoveResult::Moved, DirectionalHandoffRoute::NoOp),
              MoveFocusRouteAction::FinalizeLocalMove,
              "move_focus keeps same-lane movement local");
    expect_eq(decide_move_focus_route(true, true, true, FocusMoveResult::NoOp, DirectionalHandoffRoute::AdjacentLane),
              MoveFocusRouteAction::AdjacentLane,
              "move_focus routes empty-lane navigation to an adjacent lane");
    expect_eq(decide_move_focus_route(true, true, true, FocusMoveResult::NoOp, DirectionalHandoffRoute::CreateLane),
              MoveFocusRouteAction::CreateLane,
              "move_focus can create an empty lane when routing into blank space");
    expect_eq(decide_move_focus_route(true, false, true, FocusMoveResult::CrossMonitor, DirectionalHandoffRoute::AdjacentLane),
              MoveFocusRouteAction::AdjacentLane,
              "move_focus prefers adjacent lanes over monitor escape on lane directions");
    expect_eq(decide_move_focus_route(true, false, true, FocusMoveResult::CrossMonitor, DirectionalHandoffRoute::NoOp),
              MoveFocusRouteAction::CrossMonitor,
              "move_focus returns cross-monitor handoff for monitor edges");
    expect_eq(decide_move_focus_route(false, false, false, FocusMoveResult::NoOp, DirectionalHandoffRoute::NoOp),
              MoveFocusRouteAction::DispatchBuiltin,
              "move_focus falls back to builtin routing when no lane exists");
    expect_true(should_cross_monitor_from_empty_lane(true, false, true),
                "empty lanes can cross monitors on local directions when a monitor exists");
    expect_true(!should_cross_monitor_from_empty_lane(true, true, true),
                "empty lanes keep lane-axis routing priority when moving between lanes");
    expect_true(!should_cross_monitor_from_empty_lane(true, false, false),
                "empty lanes do not force cross-monitor handoff without a target monitor");

    expect_eq(decide_cross_lane_move_window_action(true, true, DirectionalHandoffRoute::AdjacentLane),
              CrossLaneMoveWindowAction::AdjacentLaneTransfer,
              "movewindow transfers into adjacent lanes");
    expect_eq(decide_cross_lane_move_window_action(true, true, DirectionalHandoffRoute::CrossMonitor),
              CrossLaneMoveWindowAction::CrossMonitorTransfer,
              "movewindow routes to cross-monitor handoff when needed");
    expect_eq(decide_cross_lane_move_window_action(false, true, DirectionalHandoffRoute::AdjacentLane),
              CrossLaneMoveWindowAction::BuiltinFallback,
              "movewindow falls back to builtin dispatch when current window is missing");

    expect_true(should_mark_special_ephemeral_lane_for_restore(true, false, true, true),
                "hidden special workspace marks an empty ephemeral lane for restore");
    expect_true(!should_mark_special_ephemeral_lane_for_restore(true, true, true, true),
                "visible special workspace does not mark an empty ephemeral lane for restore");
    expect_true(!should_mark_special_ephemeral_lane_for_restore(true, false, true, false),
                "non-empty lane is not marked for restore when hiding special workspace");
    expect_true(should_restore_marked_special_ephemeral_lane(true, true, true, true, true),
                "reopened special workspace restores a marked empty ephemeral lane");
    expect_true(!should_restore_marked_special_ephemeral_lane(true, true, false, true, true),
                "visible special workspace does not restore without a pending hidden state");
}

void test_dispatch_logic() {
    using namespace CanvasLayoutInternal;

    FakeDispatcherRuntime runtime;
    runtime.registryAvailable = false;
    runtime.knownDispatchers = {"movefocus"};
    expect_true(!can_invoke_dispatcher(runtime, "movefocus", "l", "dispatch_test"),
                "dispatcher helper rejects unavailable registry");

    runtime.registryAvailable = true;
    expect_true(!can_invoke_dispatcher(runtime, "movefocus", "", "dispatch_test"),
                "dispatcher helper rejects empty args");
    expect_true(!can_invoke_dispatcher(runtime, "movewindow", "l", "dispatch_test"),
                "dispatcher helper rejects missing dispatchers");

    expect_true(can_invoke_dispatcher(runtime, "movefocus", "l", "dispatch_test"),
                "dispatcher helper accepts available dispatchers");
    expect_true(invoke_dispatcher(runtime, "movefocus", "l", "dispatch_test"),
                "dispatcher helper invokes runtime callbacks");
    expect_eq(runtime.invocations.size(), std::size_t{1},
              "dispatcher helper records exactly one successful invocation");
    expect_eq(runtime.invocations[0].first, std::string("movefocus"),
              "dispatcher helper keeps dispatcher name");
    expect_eq(runtime.invocations[0].second, std::string("l"),
              "dispatcher helper keeps dispatcher arg");

    runtime.invocationSucceeds = false;
    expect_true(!invoke_dispatcher(runtime, "movefocus", "l", "dispatch_test"),
                "dispatcher helper propagates runtime invocation failures");
    expect_eq(runtime.invocations.size(), std::size_t{1},
              "dispatcher helper does not record failed invocations");
}

void test_owner_index() {
    ScrollerCore::OwnerIndex<int, FakeOwner> index;
    FakeOwner stackA{.id = 1, .keys = {1, 2}};
    FakeOwner stackB{.id = 2, .keys = {3}};

    index.remember_owner(&stackA, [&](auto &&remember) {
        for (const auto key : stackA.keys)
            remember(key);
    });
    index.remember_owner(&stackB, [&](auto &&remember) {
        for (const auto key : stackB.keys)
            remember(key);
    });

    expect_true(index.find_valid(2, [&](FakeOwner *owner) { return owner && owner->owns(2); }) == &stackA,
                "owner index returns the cached owner when it still owns the key");
    expect_true(index.matches_expected([&](auto &&addExpected) {
        addExpected(1, &stackA);
        addExpected(2, &stackA);
        addExpected(3, &stackB);
    }), "owner index matches the expected owner mapping after initial population");

    stackA.keys = {1};
    expect_true(index.find_valid(2, [&](FakeOwner *owner) { return owner && owner->owns(2); }) == nullptr,
                "owner index evicts stale cached keys when the owner no longer matches");
    expect_true(index.matches_expected([&](auto &&addExpected) {
        addExpected(1, &stackA);
        addExpected(3, &stackB);
    }), "owner index keeps unrelated keys after evicting one stale entry");

    index.forget_owner(&stackA);
    expect_true(index.matches_expected([&](auto &&addExpected) {
        addExpected(3, &stackB);
    }), "owner index can forget every key that still points at one owner");

    index.clear();
    expect_true(index.matches_expected([&](auto &&) {}),
                "owner index clear removes every cached key");
}

void test_fit_size_helpers() {
    FakeFitItem itemA{.span = 10.0, .visible = false};
    FakeFitItem itemB{.span = 20.0, .visible = true};
    FakeFitItem itemC{.span = 30.0, .visible = true};
    FakeFitItem itemD{.span = 40.0, .visible = false};
    List<FakeFitItem *> items;
    items.push_back(&itemA);
    items.push_back(&itemB);
    items.push_back(&itemC);
    items.push_back(&itemD);

    auto *active = items.first()->next()->next();
    const auto isVisible = [](ListNode<FakeFitItem *> *node) {
        return node && node->data() && node->data()->visible;
    };

    const auto [activeFrom, activeTo] = ScrollerCore::select_fit_size_range(
        FitSize::Active, items.first(), items.last(), active, isVisible);
    expect_true(activeFrom == active && activeTo == active,
                "fit-size helper selects only the active node for active mode");

    const auto [visibleFrom, visibleTo] = ScrollerCore::select_fit_size_range(
        FitSize::Visible, items.first(), items.last(), active, isVisible);
    expect_true(visibleFrom == items.first()->next(),
                "fit-size helper finds the first visible node");
    expect_true(visibleTo == items.last()->prev(),
                "fit-size helper finds the last visible node");

    const auto [toBegFrom, toBegTo] = ScrollerCore::select_fit_size_range(
        FitSize::ToBeg, items.first(), items.last(), active, isVisible);
    expect_true(toBegFrom == items.first() && toBegTo == active,
                "fit-size helper keeps the head-to-active range");

    const auto normalized = ScrollerCore::normalize_fit_size_range(
        visibleFrom,
        visibleTo,
        100.0,
        [](ListNode<FakeFitItem *> *node) {
            return node->data()->span;
        },
        [](ListNode<FakeFitItem *> *node, double scaledSpan) {
            node->data()->span = scaledSpan;
        });
    expect_true(normalized, "fit-size helper normalizes non-empty ranges");
    expect_near(itemB.span, 40.0, 1e-9,
                "fit-size helper preserves proportional span for the first visible node");
    expect_near(itemC.span, 60.0, 1e-9,
                "fit-size helper preserves proportional span for the second visible node");

    FakeFitItem zeroA{.span = 0.0, .visible = true};
    FakeFitItem zeroB{.span = 0.0, .visible = true};
    List<FakeFitItem *> zeros;
    zeros.push_back(&zeroA);
    zeros.push_back(&zeroB);
    expect_true(!ScrollerCore::normalize_fit_size_range(
                    zeros.first(),
                    zeros.last(),
                    80.0,
                    [](ListNode<FakeFitItem *> *node) {
                        return node->data()->span;
                    },
                    [](ListNode<FakeFitItem *> *node, double scaledSpan) {
                        node->data()->span = scaledSpan;
                    }),
                "fit-size helper rejects zero-total ranges");
}

void test_list_move_only() {
    static_assert(!std::is_copy_constructible_v<List<int>>);
    static_assert(!std::is_copy_assignable_v<List<int>>);
    static_assert(std::is_move_constructible_v<List<int>>);
    static_assert(std::is_move_assignable_v<List<int>>);

    List<int> original;
    original.push_back(1);
    original.push_back(2);

    List<int> moved(std::move(original));
    expect_true(original.empty(), "moved-from list becomes empty after move construction");
    expect_eq(moved.size(), std::size_t{2}, "move construction preserves list size");
    expect_eq(moved.first()->data(), 1, "move construction preserves first node data");
    expect_eq(moved.last()->data(), 2, "move construction preserves last node data");

    List<int> assigned;
    assigned.push_back(99);
    assigned = std::move(moved);
    expect_true(moved.empty(), "moved-from list becomes empty after move assignment");
    expect_eq(assigned.size(), std::size_t{2}, "move assignment replaces the destination contents");
    expect_eq(assigned.first()->data(), 1, "move assignment preserves first node data");
    expect_eq(assigned.last()->data(), 2, "move assignment preserves last node data");
}

void test_overview_target_selection_across_monitors() {
    const std::vector<OverviewLogic::TargetCandidate> targets = {
        {.monitorId = 1, .box = {0.0, 0.0, 100.0, 100.0}},
        {.monitorId = 1, .box = {120.0, 0.0, 100.0, 100.0}},
        {.monitorId = 2, .box = {400.0, 0.0, 100.0, 100.0}},
    };

    const auto next = OverviewLogic::pickTargetIndex(targets, 1, Direction::Right);
    expect_true(next.has_value() && *next == 2,
                "overview target selection crosses to the next monitor when the nearest target is there");
}

void test_overview_empty_target_region_selection() {
    const std::vector<OverviewLogic::RegionCandidate> regions = {
        {.monitorId = 1, .box = {0.0, 0.0, 300.0, 300.0}},
        {.monitorId = 2, .box = {320.0, 0.0, 300.0, 300.0}},
    };

    const ScrollerCore::Box sourceBox(260.0, 120.0, 80.0, 80.0);
    const auto regionIndex = OverviewLogic::pickRegionIndexForSyntheticTarget(regions, 0, sourceBox, Direction::Right);
    expect_true(regionIndex.has_value() && *regionIndex == 1,
                "overview empty target chooses the adjacent monitor region when crossing monitor bounds");
}

void test_overview_empty_accept_plan() {
    const auto plan = OverviewLogic::buildEmptyAcceptPlan(7, 42);
    expect_eq(plan.size(), static_cast<size_t>(2), "overview empty accept plan emits two steps");
    expect_eq(plan[0].type, OverviewLogic::AcceptActionType::FocusMonitor,
              "overview empty accept plan focuses the monitor first");
    expect_eq(plan[0].monitorId, 7, "overview empty accept plan keeps the requested monitor id");
    expect_eq(plan[1].type, OverviewLogic::AcceptActionType::Workspace,
              "overview empty accept plan switches workspace second");
    expect_eq(plan[1].workspaceId, static_cast<OverviewLogic::WorkspaceId>(42),
              "overview empty accept plan keeps the requested workspace id");
}

void test_overview_window_accept_plan() {
    const auto plan = OverviewLogic::buildWorkspaceAcceptPlan(5, 17, false);
    expect_eq(plan.size(), static_cast<size_t>(2), "overview window accept plan emits two steps");
    expect_eq(plan[0].type, OverviewLogic::AcceptActionType::FocusMonitor,
              "overview window accept plan focuses the monitor first");
    expect_eq(plan[0].monitorId, 5, "overview window accept plan keeps the requested monitor id");
    expect_eq(plan[1].type, OverviewLogic::AcceptActionType::Workspace,
              "overview window accept plan switches normal workspace second");
    expect_eq(plan[1].workspaceId, static_cast<OverviewLogic::WorkspaceId>(17),
              "overview window accept plan keeps the requested workspace id");
}

void test_overview_special_workspace_accept_plan() {
    const auto plan = OverviewLogic::buildWorkspaceAcceptPlan(3, 88, true);
    expect_eq(plan.size(), static_cast<size_t>(2), "overview special accept plan emits two steps");
    expect_eq(plan[0].type, OverviewLogic::AcceptActionType::FocusMonitor,
              "overview special accept plan focuses the monitor first");
    expect_eq(plan[1].type, OverviewLogic::AcceptActionType::ToggleSpecialWorkspace,
              "overview special accept plan toggles special workspace second");
    expect_eq(plan[1].workspaceId, static_cast<OverviewLogic::WorkspaceId>(88),
              "overview special accept plan keeps the requested workspace id");
}

} // namespace

int main() {
    test_interval();
    test_direction_helpers();
    test_parse_helpers();
    test_anchor_selection();
    test_overview_projection();
    test_layout_profile();
    test_monitor_geometry();
    test_monitor_space_orientation();
    test_handoff_state();
    test_route_logic();
    test_dispatch_logic();
    test_owner_index();
    test_fit_size_helpers();
    test_list_move_only();
    test_overview_target_selection_across_monitors();
    test_overview_empty_target_region_selection();
    test_overview_empty_accept_plan();
    test_overview_window_accept_plan();
    test_overview_special_workspace_accept_plan();

    if (failures != 0) {
        std::cerr << failures << " logic test(s) failed\n";
        return EXIT_FAILURE;
    }

    std::cout << "All logic tests passed\n";
    return EXIT_SUCCESS;
}
