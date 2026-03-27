/**
 * @file layout.h
 * @brief Layout controller for Hyprscroller's tiled workspace orchestration.
 *
 * Declares `CanvasLayout`, the Hyprland tiled algorithm implementation that
 * owns the lanes of a canvas, translates Hyprland layout callbacks, and exposes
 * command-facing operations through dispatchers.
 */
#pragma once

#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

#include <hyprland/src/layout/algorithm/TiledAlgorithm.hpp>
#include <hyprland/src/layout/target/Target.hpp>
#include <hyprland/src/helpers/signal/Signal.hpp>

#include "../../core/types.h"
#include "../../list.h"
#include "handoff_state.h"

class Lane;

struct CanvasOverviewSnapshotWindow {
    PHLWINDOW         window = nullptr;
    ScrollerCore::Box box;
};

struct CanvasOverviewSnapshot {
    WORKSPACEID                           workspaceId = WORKSPACE_INVALID;
    int                                   monitorId = MONITOR_INVALID;
    std::vector<CanvasOverviewSnapshotWindow> windows;
};

/**
 * @brief Tiled layout controller for one canvas/workspace instance.
 *
 * `CanvasLayout` is the integration layer between Hyprland's tiled algorithm
 * API and the plugin's internal model. It owns the ordered set of lanes for a
 * canvas, keeps active-lane state in sync with Hyprland focus, and exposes the
 * dispatcher-facing operations used by the plugin.
 */
class CanvasLayout : public Layout::ITiledAlgorithm {
public:
    // Public hooks required by Hyprland's tiled algorithm interface.
    void                             newTarget(SP<Layout::ITarget> target) override;
    void                             movedTarget(SP<Layout::ITarget> target, std::optional<Vector2D> focalPoint = std::nullopt) override;
    void                             removeTarget(SP<Layout::ITarget> target) override;
    void                             resizeTarget(const Vector2D &delta, SP<Layout::ITarget> target, Layout::eRectCorner corner = Layout::CORNER_NONE) override;
    void                             recalculate() override;
    std::expected<void, std::string>  layoutMsg(const std::string_view& sv) override;
    std::optional<Vector2D>          predictSizeForNewTarget() override;
    SP<Layout::ITarget>              getNextCandidate(SP<Layout::ITarget> old) override;
    void                             swapTargets(SP<Layout::ITarget> a, SP<Layout::ITarget> b) override;
    void                             moveTargetInDirection(SP<Layout::ITarget> t, Math::eDirection direction, bool silent = false) override;

    // Internal compatibility helpers used by LayoutAlgorithm dispatch and
    // legacy callback paths.
    void onEnable();
    void onDisable();
    // Called when a tiled window is first mapped.
    void onWindowCreatedTiling(PHLWINDOW, Math::eDirection = Math::DIRECTION_DEFAULT);
    // Return true if the layout currently manages this window.
    bool isWindowTiled(PHLWINDOW);
    // Called when a tiled window is unmapped.
    void onWindowRemovedTiling(PHLWINDOW);
    // Recompute geometry for monitor-specific constraints or DPI/workspace changes.
    void recalculateMonitor(const int &monitor_id);
    // Recompute layout for a specific window (for toggles like pseudo and resize).
    void recalculateWindow(PHLWINDOW);
    void resizeActiveWindow(PHLWINDOW, const Vector2D &delta, Layout::eRectCorner = Layout::CORNER_NONE, PHLWINDOW pWindow = nullptr);
    // Move current active item using directional command aliases.
    void moveWindowTo(PHLWINDOW, const std::string &direction, bool silent = false);
    // Swaps cached window metadata when plugin-level mapping changes.
    void switchWindows(PHLWINDOW, PHLWINDOW);
    // Compatibility no-op for split ratio events.
    void alterSplitRatio(PHLWINDOW, float, bool);
    PHLWINDOW getNextWindowCandidate(PHLWINDOW);
    // Keep active lane/stack selection synchronized with focus changes.
    void onWindowFocusChange(PHLWINDOW);
    void replaceWindowDataWith(PHLWINDOW from, PHLWINDOW to);
    Vector2D predictSizeForNewWindowTiled();
    // Refresh stale special-workspace lane state before a dispatcher acts on this layout.
    void prepareForActionContext();
    // Synchronize and relayout this canvas so overview snapshots read current lane/stack geometry.
    void prepareForOverviewSnapshot();
    // Build a read-only snapshot of the current logical lane/stack window geometry.
    CanvasOverviewSnapshot buildOverviewSnapshot() const;

    // New dispatchers: command-facing control surface from Hyprland config.
    void cycle_window_size(int workspace, int step);
    void move_focus(int workspace, Direction);
    void move_window(int workspace, Direction);
    void align_window(int workspace, Direction);
    void admit_window_left(int workspace);
    void expel_window_right(int workspace);
    void set_mode(int workspace, Mode);
    void fit_size(int workspace, FitSize);
    void toggle_overview(int workspace);
    void toggle_fullscreen(int workspace);
    void create_lane(int workspace, Direction);
    void focus_lane(int workspace, Direction);

    // Mark helpers: lightweight named bookmarks for focused windows.
    void marks_add(const std::string &name);
    void marks_delete(const std::string &name);
    void marks_visit(const std::string &name);
    void marks_reset();

private:
    // Resolve the workspace that owns this canvas instance.
    PHLWORKSPACE getCanvasWorkspace() const;
    // Return the currently active lane, defaulting to the first lane when needed.
    Lane *getActiveLane();
    // Point the canvas at a new active lane.
    void setActiveLane(Lane *lane);
    // Find the lane that currently owns a window.
    Lane *getLaneForWindow(PHLWINDOW window);
    // Remember or update the cached lane owner for one window.
    void rememberWindowLane(PHLWINDOW window, Lane *lane);
    // Remove one window from the cached lane-owner index.
    void forgetWindowLane(PHLWINDOW window);
    // Refresh cached ownership for every window currently in the given lane.
    void rememberLaneWindows(Lane *lane);
    // Drop every cached entry that still points at the given lane pointer.
    void forgetLaneWindows(Lane *lane);
    // Validate the debug window -> lane cache against the actual lane contents.
    void debugVerifyLaneCache() const;
    // Return the list node for a lane inside this canvas.
    ListNode<Lane *> *getLaneNode(Lane *lane) const;
    // Return the zero-based index of a lane for logs and paging math.
    int laneIndexOf(Lane *lane) const;
    // Count lanes in the current canvas.
    size_t laneCount() const;
    // Resolve the monitor currently showing this canvas.
    PHLMONITOR getVisibleCanvasMonitor(PHLMONITOR fallbackMonitor = nullptr) const;
    // Relayout this canvas on its visible monitor.
    void relayoutVisibleCanvas(PHLMONITOR fallbackMonitor = nullptr);
    // Recalculate all lanes inside the canvas against one monitor.
    void relayoutCanvas(PHLMONITOR monitor, bool honor_fullscreen);
    // Sweep all hidden special canvases so stale empty-lane state gets marked even when the hidden canvas is not ticking.
    void syncHiddenSpecialWorkspaceCanvases();
    // Track hidden/visible transitions for special-workspace empty lanes and restore focus when they reappear.
    bool syncSpecialWorkspaceVisibilityState(PHLMONITOR visibleMonitor);
    // Sync active lane/window state from Hyprland's remembered workspace focus.
    void syncActiveStateFromWorkspaceFocus();
    // Adopt the lane containing a newly focused window.
    bool adoptFocusedLane(PHLWINDOW focusedWindow, PHLMONITOR fallbackMonitor = nullptr);
    // Resolve the target window/lane selected for a cross-monitor focus handoff.
    PHLWINDOW resolveCrossMonitorFocusTarget(CanvasLayout *targetLayout, PHLMONITOR monitor, WORKSPACEID workspaceId, Direction direction, PHLWINDOW sourceWindow, Lane **targetLane, const char **selection);
    // Apply active-lane state and relayout on the destination canvas after cross-monitor focus selection.
    void activateCrossMonitorFocusTarget(CanvasLayout *targetLayout, Lane *targetLane, PHLWINDOW targetWindow, PHLMONITOR fallbackMonitor);
    // Execute the full cross-monitor focus handoff for directional navigation.
    void handoffFocusAcrossMonitor(int workspace, Direction direction, PHLWINDOW sourceWindow, PHLMONITOR sourceMonitor, WORKSPACEID sourceActiveWorkspaceId, WORKSPACEID sourceSpecialWorkspaceId, ListNode<Lane *> *sourceLaneNode, PHLMONITOR targetMonitor);
    // Resolve the next monitor in a logical direction from a source monitor.
    PHLMONITOR directionalMoveTargetMonitor(PHLMONITOR sourceMonitor, Direction direction) const;
    // Execute the full cross-monitor movewindow handoff and relayout/focus updates.
    bool handoffMoveWindowAcrossMonitor(int workspace, Direction direction, Lane *sourceLane, PHLWINDOW currentWindow, PHLMONITOR sourceMonitor, PHLMONITOR targetMonitor);
    // Move the active window payload into an adjacent lane and finish source cleanup.
    void transferMoveWindowToAdjacentLane(Lane *sourceLane, PHLWINDOW currentWindow, ListNode<Lane *> *targetLaneNode, Direction direction, PHLMONITOR sourceMonitor);
    // Move the active window payload into a newly created lane and finish source cleanup.
    void transferMoveWindowToNewLane(Lane *sourceLane, PHLWINDOW currentWindow, PHLMONITOR sourceMonitor, Mode mode, Direction direction);
    // Handle movement that stays on the lane's primary axis unless it crosses monitors.
    void handleMoveWindowWithinLane(int workspace, Direction direction, Lane *lane, PHLWINDOW currentWindow, PHLMONITOR sourceMonitor);
    // Handle movement that routes through adjacent lanes, monitor handoff, or lane creation.
    void handleMoveWindowAcrossLanes(int workspace, Direction direction, Lane *lane, PHLWINDOW currentWindow, PHLMONITOR sourceMonitor, Mode mode);
    // Focus the adjacent lane chosen by directional routing and clean up the source lane if needed.
    void focusAdjacentLane(int workspace, Direction direction, ListNode<Lane *> *sourceLaneNode, PHLMONITOR sourceMonitor, ListNode<Lane *> *targetLaneNode);
    // Create a temporary empty lane used when focus moves into blank space.
    void createEphemeralLaneForFocus(int workspace, Direction direction, PHLMONITOR sourceMonitor, Mode mode, ListNode<Lane *> *anchor);
    // Finish a successful local focus movement by logging and switching focus.
    void finalizeLocalFocusMove(int workspace, Direction direction, Lane *lane, const char *moveResultName);
    // Drop an empty lane and resolve a valid replacement active lane.
    bool dropEmptyLane(ListNode<Lane *> *laneNode, Lane *preferredLane = nullptr, PHLMONITOR fallbackMonitor = nullptr, bool ephemeralOnly = false);
    // Compatibility wrapper used by older ephemeral-lane call sites.
    bool dropEmptyEphemeralLane(ListNode<Lane *> *laneNode, Lane *preferredLane = nullptr, PHLMONITOR fallbackMonitor = nullptr);
    // Choose the active lane that should survive after lane removal.
    Lane *resolveActiveLaneAfterRemoval(ListNode<Lane *> *laneNode, PHLWINDOW removedWindow);
    // Insert a lane into canvas ordering according to directional semantics.
    ListNode<Lane *> *insertLaneNode(Lane *lane, Direction direction, ListNode<Lane *> *anchor = nullptr);
    // Return the active lane, creating one if this canvas is still empty.
    Lane *ensureActiveLane(PHLMONITOR monitor, Mode mode);
    // Mark a window so target callbacks defer to explicit cross-monitor insertion.
    void rememberManualCrossMonitorInsertion(PHLWINDOW window);
    // Clear the explicit cross-monitor insertion handoff marker.
    void forgetManualCrossMonitorInsertion(PHLWINDOW window);
    // Return true when target callbacks should skip auto-registering this window.
    bool hasPendingManualCrossMonitorInsertion(PHLWINDOW window) const;
    // Request one-shot suppression of workspace-focus sync on the next command.
    void requestWorkspaceFocusSyncSuppression();
    // Return the next active-lane sync policy and consume any one-shot suppression.
    ActiveLaneSyncPolicy consumeActiveLaneSyncPolicy();
    // Clear transient focus/cross-monitor handoff state.
    void resetHandoffState();
    // Finish a lane transfer by pruning the source lane, relayouting, and focusing the active lane.
    void finishLaneTransfer(ListNode<Lane *> *sourceLaneNode, PHLMONITOR sourceMonitor = nullptr, bool ephemeralOnly = false, bool warpCursor = true);

    template <typename Fn>
    // Execute a command against the current active lane with optional focus sync.
    void withActiveLane(ActiveLaneSyncPolicy syncPolicy, Fn&& fn) {
        if (syncPolicy == ActiveLaneSyncPolicy::WorkspaceFocus)
            syncActiveStateFromWorkspaceFocus();

        if (auto *lane = getActiveLane())
            std::forward<Fn>(fn)(lane);
    }

    // Optional Hyprland focus listener used to keep canvas state synchronized.
    CHyprSignalListener m_focusCallback;
    // Workspace-active listener used to observe special workspace hide/show transitions.
    CHyprSignalListener m_workspaceActiveCallback;
    // Currently active lane inside this canvas.
    ListNode<Lane *> *activeLane = nullptr;
    // Ordered lanes that make up the current canvas.
    List<Lane *> lanes;
    // Cached window -> lane index used to avoid repeated whole-canvas scans.
    std::unordered_map<uintptr_t, Lane *> laneByWindow;
    // Concentrated one-shot focus and cross-monitor handoff state.
    HandoffState handoffState;
    // Remember whether a hidden special workspace needs to restore from a stale empty lane when shown again.
    bool specialEphemeralLaneRestorePending = false;
};
