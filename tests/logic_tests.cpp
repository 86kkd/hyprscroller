#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core/direction.h"
#include "core/interval.h"
#include "core/layout_math.h"
#include "core/layout_profile.h"
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
    test_monitor_space_orientation();
    test_handoff_state();
    test_route_logic();
    test_dispatch_logic();
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
