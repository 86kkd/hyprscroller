#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "layout/canvas/dispatch_logic.h"
#include "layout/canvas/handoff_state.h"
#include "layout/canvas/route_logic.h"

#include "test_suite.h"
#include "test_support.h"

namespace {

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

} // namespace

void run_layout_logic_tests() {
    test_handoff_state();
    test_route_logic();
    test_dispatch_logic();
}
