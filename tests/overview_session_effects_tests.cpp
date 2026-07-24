#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "overview/session/effects_runtime.h"

#include "test_suite.h"
#include "test_support.h"

namespace {

constexpr WORKSPACEID INVALID_WORKSPACE_ID = static_cast<WORKSPACEID>(-1);
constexpr MONITORID INVALID_MONITOR_ID = static_cast<MONITORID>(-1);

template <typename T>
SP<T> make_opaque_handle() {
    return reinterpretPointerCast<T>(makeShared<int>(0));
}

struct FakeSessionEffectsRuntime final : Overview::SessionEffects::Runtime {
    struct MonitorState {
        MONITORID id = INVALID_MONITOR_ID;
    };

    struct WorkspaceState {
        WORKSPACEID id = INVALID_WORKSPACE_ID;
        MONITORID monitorId = INVALID_MONITOR_ID;
        bool special = false;
        PHLWINDOW lastFocusedWindow = nullptr;
        PHLMONITOR visibleMonitor = nullptr;
        bool snapshotPrepared = false;
    };

    struct WindowState {
        MONITORID monitorId = INVALID_MONITOR_ID;
        bool mapped = true;
    };

    WORKSPACEID currentWorkspace = INVALID_WORKSPACE_ID;
    PHLMONITOR cursorMonitor = nullptr;
    bool focusMonitorWorkspaceResult = true;
    bool switchToWindowResult = true;

    mutable std::vector<std::string> callLog;
    mutable std::unordered_map<const void*, MonitorState> monitors;
    mutable std::unordered_map<const void*, WorkspaceState> workspaces;
    mutable std::unordered_map<const void*, WindowState> windows;
    std::unordered_map<MONITORID, PHLMONITOR> monitorsById;
    std::unordered_map<WORKSPACEID, PHLWORKSPACE> workspacesById;
    std::vector<PHLWORKSPACE> workspaceOrder;

    mutable MONITORID lastFocusedMonitorId = INVALID_MONITOR_ID;
    mutable WORKSPACEID lastFocusedWorkspaceId = INVALID_WORKSPACE_ID;
    mutable WORKSPACEID lastFallbackWorkspaceId = INVALID_WORKSPACE_ID;
    mutable bool lastRequireMonitorFocus = true;
    mutable const char* lastFocusContext = nullptr;
    mutable const void* lastSyncedWindow = nullptr;
    mutable MONITORID lastSyncedMonitorId = INVALID_MONITOR_ID;
    mutable const void* lastSwitchedWindow = nullptr;
    mutable bool lastSwitchWarpCursor = false;

    WORKSPACEID currentWorkspaceId() const override {
        return currentWorkspace;
    }

    PHLWORKSPACE getWorkspaceByID(WORKSPACEID workspaceId) const override {
        const auto it = workspacesById.find(workspaceId);
        return it == workspacesById.end() ? nullptr : it->second;
    }

    std::vector<PHLWORKSPACE> getWorkspaces() const override {
        return workspaceOrder;
    }

    PHLMONITOR getMonitorFromID(MONITORID monitorId) const override {
        const auto it = monitorsById.find(monitorId);
        return it == monitorsById.end() ? nullptr : it->second;
    }

    PHLMONITOR getMonitorFromCursor() const override {
        return cursorMonitor;
    }

    MONITORID monitorId(PHLMONITOR monitor) const override {
        const auto it = monitors.find(monitor.get());
        return it == monitors.end() ? INVALID_MONITOR_ID : it->second.id;
    }

    WORKSPACEID workspaceId(PHLWORKSPACE workspace) const override {
        const auto it = workspaces.find(workspace.get());
        return it == workspaces.end() ? INVALID_WORKSPACE_ID : it->second.id;
    }

    MONITORID workspaceMonitorId(PHLWORKSPACE workspace) const override {
        const auto it = workspaces.find(workspace.get());
        return it == workspaces.end() ? INVALID_MONITOR_ID : it->second.monitorId;
    }

    bool isWorkspaceSpecial(PHLWORKSPACE workspace) const override {
        const auto it = workspaces.find(workspace.get());
        return it != workspaces.end() && it->second.special;
    }

    PHLWINDOW workspaceLastFocusedWindow(PHLWORKSPACE workspace) const override {
        const auto it = workspaces.find(workspace.get());
        return it == workspaces.end() ? nullptr : it->second.lastFocusedWindow;
    }

    MONITORID windowMonitorId(PHLWINDOW window) const override {
        const auto it = windows.find(window.get());
        return it == windows.end() ? INVALID_MONITOR_ID : it->second.monitorId;
    }

    bool isWindowMapped(PHLWINDOW window) const override {
        const auto it = windows.find(window.get());
        return it != windows.end() && it->second.mapped;
    }

    PHLMONITOR visibleMonitorForWorkspace(PHLWORKSPACE workspace) const override {
        const auto it = workspaces.find(workspace.get());
        return it == workspaces.end() ? nullptr : it->second.visibleMonitor;
    }

    void syncCanvasTargetWindow(PHLWORKSPACE workspace, PHLWINDOW window, MONITORID monitorId) const override {
        callLog.emplace_back("sync_canvas_target_window");
        lastFocusedWorkspaceId = workspaceId(workspace);
        lastSyncedWindow = window ? static_cast<const void*>(window.get()) : nullptr;
        lastSyncedMonitorId = monitorId;
    }

    void prepareWorkspaceSnapshot(PHLWORKSPACE workspace) const override {
        callLog.emplace_back("prepare_workspace_snapshot");
        workspaces[workspace.get()].snapshotPrepared = true;
    }

    bool focusMonitorWorkspace(PHLMONITOR monitor, PHLWORKSPACE workspace, WORKSPACEID fallbackWorkspaceId,
                               bool requireMonitorFocus, const char* context) const override {
        callLog.emplace_back("focus_monitor_workspace");
        lastFocusedMonitorId = monitorId(monitor);
        lastFocusedWorkspaceId = workspaceId(workspace);
        lastFallbackWorkspaceId = fallbackWorkspaceId;
        lastRequireMonitorFocus = requireMonitorFocus;
        lastFocusContext = context;
        return focusMonitorWorkspaceResult;
    }

    bool switchToWindow(PHLWINDOW window, bool warpCursor) const override {
        callLog.emplace_back("switch_to_window");
        lastSwitchedWindow = window ? static_cast<const void*>(window.get()) : nullptr;
        lastSwitchWarpCursor = warpCursor;
        return switchToWindowResult;
    }

    PHLMONITOR addMonitor(MONITORID id) {
        auto monitor = make_opaque_handle<Monitor::CMonitor>();
        monitors.emplace(monitor.get(), MonitorState{.id = id});
        monitorsById.emplace(id, monitor);
        return monitor;
    }

    PHLWORKSPACE addWorkspace(WORKSPACEID id, MONITORID monitorId, bool special = false) {
        auto workspace = make_opaque_handle<CWorkspace>();
        workspaces.emplace(workspace.get(), WorkspaceState{
            .id = id,
            .monitorId = monitorId,
            .special = special,
        });
        workspacesById.emplace(id, workspace);
        workspaceOrder.push_back(workspace);
        return workspace;
    }

    PHLWINDOW addWindow(MONITORID monitorId, bool mapped = true) {
        auto window = make_opaque_handle<Desktop::View::CWindow>();
        windows.emplace(window.get(), WindowState{
            .monitorId = monitorId,
            .mapped = mapped,
        });
        return window;
    }
};

void expect_call_sequence(const std::vector<std::string>& actual,
                          const std::vector<std::string>& expected,
                          std::string_view message) {
    expect_eq(actual.size(), expected.size(), message);
    if (actual.size() != expected.size())
        return;

    for (size_t index = 0; index < expected.size(); ++index)
        expect_eq(actual[index], expected[index], message);
}

void test_accept_window_target_runs_focus_sync_switch_chain() {
    FakeSessionEffectsRuntime runtime;
    const auto workspaceMonitor = runtime.addMonitor(1);
    const auto windowMonitor = runtime.addMonitor(2);
    const auto workspace = runtime.addWorkspace(7, 1);
    const auto window = runtime.addWindow(2, true);
    runtime.workspaces[workspace.get()].visibleMonitor = workspaceMonitor;

    const Overview::Target selection{
        .type = Overview::TargetType::Window,
        .workspaceId = 7,
        .monitorId = 99,
        .window = window,
        .box = {},
        .synthetic = false,
    };

    expect_true(Overview::SessionEffects::acceptTarget(runtime, selection),
                "acceptTarget succeeds for a valid window target");
    expect_eq(runtime.lastFocusedMonitorId, static_cast<MONITORID>(2),
              "acceptTarget prefers the live window monitor over stale target metadata");
    expect_eq(runtime.lastFocusedWorkspaceId, static_cast<WORKSPACEID>(7),
              "acceptTarget focuses the target workspace before syncing");
    expect_eq(runtime.lastFallbackWorkspaceId, static_cast<WORKSPACEID>(7),
              "acceptTarget forwards the workspace id as fallback focus state");
    expect_true(!runtime.lastRequireMonitorFocus,
                "acceptTarget lets window activation finish cross-monitor handoff");
    expect_eq(runtime.lastSyncedWindow, static_cast<const void*>(window.get()),
              "acceptTarget syncs the selected window into the canvas state");
    expect_eq(runtime.lastSyncedMonitorId, static_cast<MONITORID>(2),
              "acceptTarget recalculates the canvas on the resolved monitor");
    expect_eq(runtime.lastSwitchedWindow, static_cast<const void*>(window.get()),
              "acceptTarget switches to the selected window after sync");
    expect_true(runtime.lastSwitchWarpCursor,
                "acceptTarget warps the cursor when activating the selected window");
    expect_call_sequence(runtime.callLog,
                         {"focus_monitor_workspace", "sync_canvas_target_window", "switch_to_window"},
                         "acceptTarget preserves the focus/sync/switch call order");
    (void)windowMonitor;
}

void test_accept_window_target_stops_after_focus_failure() {
    FakeSessionEffectsRuntime runtime;
    runtime.focusMonitorWorkspaceResult = false;
    runtime.addMonitor(2);
    const auto workspace = runtime.addWorkspace(7, 2);
    const auto window = runtime.addWindow(2, true);
    runtime.workspaces[workspace.get()].visibleMonitor = runtime.getMonitorFromID(2);

    const Overview::Target selection{
        .type = Overview::TargetType::Window,
        .workspaceId = 7,
        .monitorId = 2,
        .window = window,
    };

    expect_true(!Overview::SessionEffects::acceptTarget(runtime, selection),
                "acceptTarget fails when workspace focus does not resolve");
    expect_call_sequence(runtime.callLog,
                         {"focus_monitor_workspace"},
                         "acceptTarget stops before canvas sync when workspace focus fails");
}

void test_accept_empty_workspace_target_only_focuses_workspace() {
    FakeSessionEffectsRuntime runtime;
    runtime.addMonitor(4);

    const Overview::Target selection{
        .type = Overview::TargetType::EmptyWorkspace,
        .workspaceId = 12,
        .monitorId = 4,
        .window = nullptr,
        .synthetic = true,
    };

    expect_true(Overview::SessionEffects::acceptTarget(runtime, selection),
                "acceptTarget accepts synthetic empty-workspace targets");
    expect_eq(runtime.lastFocusedMonitorId, static_cast<MONITORID>(4),
              "empty workspace accept focuses the requested monitor");
    expect_eq(runtime.lastFocusedWorkspaceId, static_cast<WORKSPACEID>(INVALID_WORKSPACE_ID),
              "empty workspace accept does not require a live workspace object");
    expect_eq(runtime.lastFallbackWorkspaceId, static_cast<WORKSPACEID>(12),
              "empty workspace accept forwards the synthetic workspace id as fallback");
    expect_true(runtime.lastRequireMonitorFocus,
                "empty workspace accept still requires the target monitor to become active");
    expect_call_sequence(runtime.callLog,
                         {"focus_monitor_workspace"},
                         "empty workspace accept does not sync or switch windows");
}

void test_restore_origin_prefers_original_window_when_still_mapped() {
    FakeSessionEffectsRuntime runtime;
    const auto fallbackMonitor = runtime.addMonitor(1);
    const auto visibleMonitor = runtime.addMonitor(3);
    const auto windowMonitor = runtime.addMonitor(5);
    const auto workspace = runtime.addWorkspace(8, 1);
    const auto window = runtime.addWindow(5, true);
    runtime.workspaces[workspace.get()].visibleMonitor = visibleMonitor;

    const Overview::OriginState origin{
        .monitorId = 1,
        .workspaceId = 8,
        .window = window,
    };

    expect_true(Overview::SessionEffects::restoreOrigin(runtime, origin),
                "restoreOrigin restores the original mapped window");
    expect_eq(runtime.lastFocusedMonitorId, static_cast<MONITORID>(5),
              "restoreOrigin prefers the window's live monitor when it still exists");
    expect_true(!runtime.lastRequireMonitorFocus,
                "restoreOrigin uses relaxed monitor-focus requirements for concrete windows");
    expect_eq(runtime.lastSwitchedWindow, static_cast<const void*>(window.get()),
              "restoreOrigin switches back to the original window");
    expect_true(!runtime.lastSwitchWarpCursor,
                "restoreOrigin does not warp the cursor when restoring origin");
    expect_call_sequence(runtime.callLog,
                         {"focus_monitor_workspace", "sync_canvas_target_window", "switch_to_window"},
                         "restoreOrigin uses the same focus/sync/switch chain for mapped windows");
    (void)fallbackMonitor;
    (void)windowMonitor;
}

void test_restore_origin_falls_back_to_workspace_when_window_is_unmapped() {
    FakeSessionEffectsRuntime runtime;
    runtime.addMonitor(6);
    const auto workspace = runtime.addWorkspace(11, 6);
    const auto window = runtime.addWindow(6, false);
    runtime.workspaces[workspace.get()].visibleMonitor = runtime.getMonitorFromID(6);

    const Overview::OriginState origin{
        .monitorId = 6,
        .workspaceId = 11,
        .window = window,
    };

    expect_true(Overview::SessionEffects::restoreOrigin(runtime, origin),
                "restoreOrigin falls back to workspace focus when the original window disappeared");
    expect_eq(runtime.lastFocusedMonitorId, static_cast<MONITORID>(6),
              "restoreOrigin fallback still focuses the original monitor");
    expect_eq(runtime.lastFocusedWorkspaceId, static_cast<WORKSPACEID>(11),
              "restoreOrigin fallback focuses the original workspace");
    expect_true(runtime.lastRequireMonitorFocus,
                "workspace-only restore still requires the target monitor to become active");
    expect_call_sequence(runtime.callLog,
                         {"focus_monitor_workspace"},
                         "restoreOrigin does not sync or switch a vanished window");
}

void test_capture_origin_prefers_last_focused_window_and_visible_monitor() {
    FakeSessionEffectsRuntime runtime;
    runtime.currentWorkspace = 9;
    const auto storedMonitor = runtime.addMonitor(2);
    const auto visibleMonitor = runtime.addMonitor(4);
    const auto workspace = runtime.addWorkspace(9, 2);
    const auto window = runtime.addWindow(99, true);
    runtime.workspaces[workspace.get()].lastFocusedWindow = window;
    runtime.workspaces[workspace.get()].visibleMonitor = visibleMonitor;
    runtime.cursorMonitor = storedMonitor;

    const auto origin = Overview::SessionEffects::captureOrigin(runtime);
    expect_eq(origin.workspaceId, static_cast<WORKSPACEID>(9),
              "captureOrigin keeps the current workspace id");
    expect_eq(origin.monitorId, 4,
              "captureOrigin prefers the visible monitor for the workspace when the window monitor is unavailable");
    expect_eq(origin.window, window,
              "captureOrigin keeps the last focused window");
}

void test_prepare_snapshots_and_next_workspace_id_use_all_workspaces() {
    FakeSessionEffectsRuntime runtime;
    const auto workspaceA = runtime.addWorkspace(2, 1);
    const auto workspaceB = runtime.addWorkspace(7, 2);

    Overview::SessionEffects::prepareSnapshots(runtime);
    expect_true(runtime.workspaces[workspaceA.get()].snapshotPrepared,
                "prepareSnapshots visits the first workspace");
    expect_true(runtime.workspaces[workspaceB.get()].snapshotPrepared,
                "prepareSnapshots visits the second workspace");
    expect_eq(Overview::SessionEffects::nextWorkspaceId(runtime), static_cast<WORKSPACEID>(8),
              "nextWorkspaceId returns one past the maximum visible workspace id");
}

} // namespace

void run_overview_session_effects_tests() {
    test_accept_window_target_runs_focus_sync_switch_chain();
    test_accept_window_target_stops_after_focus_failure();
    test_accept_empty_workspace_target_only_focuses_workspace();
    test_restore_origin_prefers_original_window_when_still_mapped();
    test_restore_origin_falls_back_to_workspace_when_window_is_unmapped();
    test_capture_origin_prefers_last_focused_window_and_visible_monitor();
    test_prepare_snapshots_and_next_workspace_id_use_all_workspaces();
}
