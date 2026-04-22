#include <cstdio>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "layout/canvas/dispatch_runtime.h"
#include "layout/canvas/dispatch_logic.h"
#include "layout/canvas/handoff_state.h"
#include "layout/canvas/route_logic.h"

#include "test_suite.h"
#include "test_support.h"

namespace {

constexpr WORKSPACEID INVALID_WORKSPACE_ID = static_cast<WORKSPACEID>(-1);
constexpr MONITORID INVALID_MONITOR_ID = static_cast<MONITORID>(-1);

template <typename T>
SP<T> make_opaque_handle() {
    return reinterpretPointerCast<T>(makeShared<int>(0));
}

struct FakeDispatcherRuntime final : CanvasLayoutInternal::DispatcherRuntime {
    bool registryAvailable = true;
    bool invocationSucceeds = true;
    std::vector<std::string> knownDispatchers;
    mutable std::vector<std::pair<std::string, std::string>> invocations;
    mutable PHLMONITOR cursorMonitor = nullptr;
    bool directWorkspaceActivationSucceeds = true;
    bool workspaceDispatchActivatesWorkspace = true;
    bool focusWindowDispatchActivatesWindow = true;
    int  focusMonitorSuccessOnAttempt = 1;
    mutable int focusMonitorAttempts = 0;

    struct MonitorState {
        std::string name;
        WORKSPACEID activeWorkspaceId = INVALID_WORKSPACE_ID;
        WORKSPACEID activeSpecialWorkspaceId = INVALID_WORKSPACE_ID;
    };

    struct WorkspaceState {
        WORKSPACEID id = INVALID_WORKSPACE_ID;
        std::string selector;
        bool special = false;
    };

    struct WindowState {
        MONITORID monitorId = INVALID_MONITOR_ID;
        WORKSPACEID workspaceId = INVALID_WORKSPACE_ID;
        bool active = false;
        mutable bool warpCursorCalled = false;
    };

    mutable std::unordered_map<const void*, MonitorState> monitors;
    mutable std::unordered_map<const void*, WorkspaceState> workspaces;
    mutable std::unordered_map<const void*, WindowState> windows;
    std::unordered_map<int, PHLMONITOR> monitorsById;
    std::unordered_map<std::string, PHLMONITOR> monitorsByName;
    std::unordered_map<std::string, PHLWORKSPACE> workspacesBySelector;
    std::unordered_map<std::string, PHLWINDOW> windowsBySelector;

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

        if (std::string_view(dispatcher) == "focusmonitor") {
            ++focusMonitorAttempts;
            const auto it = monitorsByName.find(std::string(arg));
            if (it != monitorsByName.end() &&
                focusMonitorSuccessOnAttempt > 0 &&
                focusMonitorAttempts >= focusMonitorSuccessOnAttempt)
                cursorMonitor = it->second;
        } else if (std::string_view(dispatcher) == "workspace") {
            if (cursorMonitor && workspaceDispatchActivatesWorkspace) {
                const auto workspaceIt = workspacesBySelector.find(std::string(arg));
                if (workspaceIt != workspacesBySelector.end()) {
                    const auto stateIt = workspaces.find(workspaceIt->second.get());
                    if (stateIt != workspaces.end()) {
                        auto &monitorState = monitors[cursorMonitor.get()];
                        if (stateIt->second.special)
                            monitorState.activeSpecialWorkspaceId = stateIt->second.id;
                        else
                            monitorState.activeWorkspaceId = stateIt->second.id;
                    }
                } else {
                    auto &monitorState = monitors[cursorMonitor.get()];
                    monitorState.activeWorkspaceId = std::stoll(std::string(arg));
                }
            }
        } else if (std::string_view(dispatcher) == "focuswindow" && focusWindowDispatchActivatesWindow) {
            const auto it = windowsBySelector.find(std::string(arg));
            if (it != windowsBySelector.end()) {
                auto &windowState = windows[it->second.get()];
                windowState.active = true;

                const auto monitorIt = monitorsById.find(windowState.monitorId);
                if (monitorIt != monitorsById.end())
                    cursorMonitor = monitorIt->second;
            }
        }

        return true;
    }

    PHLMONITOR getMonitorFromID(int monitorId) const override {
        const auto it = monitorsById.find(monitorId);
        return it == monitorsById.end() ? nullptr : it->second;
    }

    PHLMONITOR getMonitorFromCursor() const override {
        return cursorMonitor;
    }

    bool isWindowActive(PHLWINDOW window) const override {
        const auto it = windows.find(window.get());
        return it != windows.end() && it->second.active;
    }

    std::string monitorName(PHLMONITOR monitor) const override {
        const auto it = monitors.find(monitor.get());
        return it == monitors.end() ? std::string() : it->second.name;
    }

    WORKSPACEID workspaceID(PHLWORKSPACE workspace) const override {
        const auto it = workspaces.find(workspace.get());
        return it == workspaces.end() ? INVALID_WORKSPACE_ID : it->second.id;
    }

    bool isWorkspaceSpecial(PHLWORKSPACE workspace) const override {
        const auto it = workspaces.find(workspace.get());
        return it != workspaces.end() && it->second.special;
    }

    std::string workspaceSelector(PHLWORKSPACE workspace) const override {
        const auto it = workspaces.find(workspace.get());
        return it == workspaces.end() ? std::string() : it->second.selector;
    }

    bool activateMonitorWorkspaceDirectly(PHLMONITOR monitor, PHLWORKSPACE workspace, WORKSPACEID fallbackWorkspaceId) const override {
        if (!monitor || !directWorkspaceActivationSucceeds)
            return false;

        auto &monitorState = monitors[monitor.get()];
        if (workspace) {
            const auto &workspaceState = workspaces[workspace.get()];
            if (workspaceState.special)
                monitorState.activeSpecialWorkspaceId = workspaceState.id;
            else
                monitorState.activeWorkspaceId = workspaceState.id;
        } else if (fallbackWorkspaceId != INVALID_WORKSPACE_ID) {
            monitorState.activeWorkspaceId = fallbackWorkspaceId;
        }

        return isWorkspaceActiveOnMonitor(monitor, workspace, fallbackWorkspaceId);
    }

    bool isWorkspaceActiveOnMonitor(PHLMONITOR monitor, PHLWORKSPACE workspace, WORKSPACEID fallbackWorkspaceId) const override {
        const auto it = monitors.find(monitor.get());
        if (it == monitors.end())
            return false;

        if (workspace) {
            const auto workspaceIt = workspaces.find(workspace.get());
            if (workspaceIt == workspaces.end())
                return false;

            return workspaceIt->second.special
                ? it->second.activeSpecialWorkspaceId == workspaceIt->second.id
                : it->second.activeWorkspaceId == workspaceIt->second.id;
        }

        if (fallbackWorkspaceId != INVALID_WORKSPACE_ID)
            return it->second.activeWorkspaceId == fallbackWorkspaceId;

        return true;
    }

    MONITORID windowMonitorID(PHLWINDOW window) const override {
        const auto it = windows.find(window.get());
        return it == windows.end() ? INVALID_MONITOR_ID : it->second.monitorId;
    }

    WORKSPACEID windowWorkspaceID(PHLWINDOW window) const override {
        const auto it = windows.find(window.get());
        return it == windows.end() ? INVALID_WORKSPACE_ID : it->second.workspaceId;
    }

    void warpCursorToWindow(PHLWINDOW window) const override {
        auto &state = windows[window.get()];
        state.warpCursorCalled = true;
    }

    PHLMONITOR addMonitor(int id, std::string name, WORKSPACEID activeWorkspaceId = INVALID_WORKSPACE_ID) {
        auto monitor = make_opaque_handle<CMonitor>();
        monitors.emplace(monitor.get(), MonitorState{std::move(name), activeWorkspaceId, INVALID_WORKSPACE_ID});
        monitorsById.emplace(id, monitor);
        monitorsByName.emplace(monitors[monitor.get()].name, monitor);
        return monitor;
    }

    PHLWORKSPACE addWorkspace(WORKSPACEID id, std::string selector, bool special = false) {
        auto workspace = make_opaque_handle<CWorkspace>();
        workspaces.emplace(workspace.get(), WorkspaceState{id, std::move(selector), special});
        workspacesBySelector.emplace(workspaces[workspace.get()].selector, workspace);
        return workspace;
    }

    PHLWINDOW addWindow(MONITORID monitorId, WORKSPACEID workspaceId, bool active = false) {
        auto window = make_opaque_handle<Desktop::View::CWindow>();
        windows.emplace(window.get(), WindowState{monitorId, workspaceId, active, false});

        char selector[64];
        std::snprintf(selector, sizeof(selector), "address:0x%lx",
                      reinterpret_cast<unsigned long>(window.get()));
        windowsBySelector.emplace(selector, window);
        return window;
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

void test_focus_monitor_workspace_logic() {
    using namespace CanvasLayoutInternal;

    FakeDispatcherRuntime runtime;
    runtime.knownDispatchers = {"focusmonitor", "workspace"};

    const auto primaryMonitor = runtime.addMonitor(1, "HDMI-A-1", 1);
    const auto targetMonitor = runtime.addMonitor(2, "DP-1", 3);
    const auto targetWorkspace = runtime.addWorkspace(5, "5");
    runtime.cursorMonitor = primaryMonitor;

    expect_true(focus_monitor_workspace(runtime, targetMonitor, targetWorkspace, INVALID_WORKSPACE_ID, "layout_test"),
                "focus_monitor_workspace focuses target monitor and workspace");
    expect_eq(runtime.getMonitorFromCursor(), targetMonitor,
              "focus_monitor_workspace updates cursor monitor");
    expect_true(runtime.isWorkspaceActiveOnMonitor(targetMonitor, targetWorkspace, INVALID_WORKSPACE_ID),
                "focus_monitor_workspace leaves the requested workspace active");

    FakeDispatcherRuntime missingFocusRuntime;
    missingFocusRuntime.knownDispatchers = {"focusmonitor", "workspace"};
    missingFocusRuntime.focusMonitorSuccessOnAttempt = 0;
    missingFocusRuntime.directWorkspaceActivationSucceeds = false;
    const auto sourceMonitor = missingFocusRuntime.addMonitor(1, "HDMI-A-1", 1);
    const auto otherMonitor = missingFocusRuntime.addMonitor(2, "DP-1", 2);
    const auto otherWorkspace = missingFocusRuntime.addWorkspace(7, "7");
    missingFocusRuntime.cursorMonitor = sourceMonitor;

    expect_true(!focus_monitor_workspace(missingFocusRuntime, otherMonitor, otherWorkspace, INVALID_WORKSPACE_ID, "layout_test"),
                "focus_monitor_workspace fails when monitor focus never lands on the target");

    FakeDispatcherRuntime missingWorkspaceRuntime;
    missingWorkspaceRuntime.knownDispatchers = {"focusmonitor", "workspace"};
    missingWorkspaceRuntime.directWorkspaceActivationSucceeds = false;
    missingWorkspaceRuntime.workspaceDispatchActivatesWorkspace = false;
    const auto priorMonitor = missingWorkspaceRuntime.addMonitor(1, "HDMI-A-1", 1);
    const auto workspaceMonitor = missingWorkspaceRuntime.addMonitor(2, "DP-1", 3);
    const auto workspace = missingWorkspaceRuntime.addWorkspace(9, "9");
    missingWorkspaceRuntime.cursorMonitor = priorMonitor;

    expect_true(!focus_monitor_workspace(missingWorkspaceRuntime, workspaceMonitor, workspace, INVALID_WORKSPACE_ID, "layout_test"),
                "focus_monitor_workspace fails when workspace activation never settles");
}

void test_switch_to_window_logic() {
    using namespace CanvasLayoutInternal;

    FakeDispatcherRuntime runtime;
    runtime.knownDispatchers = {"focusmonitor", "focuswindow"};

    const auto currentMonitor = runtime.addMonitor(1, "HDMI-A-1", 1);
    const auto targetMonitor = runtime.addMonitor(2, "DP-1", 2);
    const auto targetWindow = runtime.addWindow(2, 2, false);
    runtime.cursorMonitor = currentMonitor;

    expect_true(switch_to_window(runtime, targetWindow, true),
                "switch_to_window focuses the target window when dispatcher succeeds");
    expect_eq(runtime.getMonitorFromCursor(), targetMonitor,
              "switch_to_window focuses the window's monitor first");
    expect_true(runtime.isWindowActive(targetWindow),
                "switch_to_window leaves the requested window active");
    expect_true(runtime.windows[targetWindow.get()].warpCursorCalled,
                "switch_to_window warps the cursor after successful focus");

    FakeDispatcherRuntime failureRuntime;
    failureRuntime.knownDispatchers = {"focusmonitor", "focuswindow"};
    failureRuntime.focusWindowDispatchActivatesWindow = false;
    const auto source = failureRuntime.addMonitor(1, "HDMI-A-1", 1);
    failureRuntime.addMonitor(2, "DP-1", 2);
    const auto stuckWindow = failureRuntime.addWindow(2, 2, false);
    failureRuntime.cursorMonitor = source;

    expect_true(!switch_to_window(failureRuntime, stuckWindow, true),
                "switch_to_window reports failure when the target window never becomes active");
    expect_true(!failureRuntime.windows[stuckWindow.get()].warpCursorCalled,
                "switch_to_window does not warp the cursor on failed focus");
}

} // namespace

void run_layout_logic_tests() {
    test_handoff_state();
    test_route_logic();
    test_dispatch_logic();
    test_focus_monitor_workspace_logic();
    test_switch_to_window_logic();
}
