/**
 * @file focus.cpp
 * @brief Focus synchronization, directional focus movement, and monitor handoff.
 *
 * This file owns the "where should focus go next?" part of the canvas layer.
 * It keeps plugin state aligned with Hyprland focus, routes directional focus
 * requests across stacks, lanes, and monitors, and handles temporary empty-lane
 * navigation used to emulate moving into blank space.
 *
 * New-reader mental model:
 * - local focus movement happens inside `Lane`
 * - this file decides when local movement is not enough
 * - if a move exits the current lane/page, this file chooses adjacent-lane,
 *   cross-monitor, or temporary-empty-lane behavior
 */
#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/helpers/Monitor.hpp>
#include <spdlog/spdlog.h>

#include "../../core/direction.h"
#include "../../plugin/config.h"
#include "internal.h"
#include "route.h"

namespace {
// Human-readable result names used by logs after a focus route completes.
const char* focus_move_result_name(FocusMoveResult result) {
    switch (result) {
    case FocusMoveResult::Moved:
        return "moved";
    case FocusMoveResult::NoOp:
        return "noop";
    case FocusMoveResult::CrossMonitor:
        return "cross_monitor";
    }

    return "unknown";
}
} // namespace

// Synchronize the canvas active lane/window with a concrete focused window.
void CanvasLayout::onWindowFocusChange(PHLWINDOW window)
{
    const auto beforeLane = activeLane ? activeLane->data() : nullptr;
    const auto beforeWindow = beforeLane ? beforeLane->get_active_window() : nullptr;
    const auto beforeLaneIndex = laneIndexOf(beforeLane);
    const auto totalLanes = static_cast<int>(laneCount());

    if (window == nullptr) {
        spdlog::debug("onWindowFocusChange: ignored null window canvas_ws={} lanes={} before_lane={} before_lane_index={} before_window={}",
                      CanvasLayoutInternal::get_workspace_id(),
                      totalLanes,
                      static_cast<const void*>(beforeLane),
                      beforeLaneIndex,
                      static_cast<const void*>(beforeWindow ? beforeWindow.get() : nullptr));
        return;
    }

    auto lane = getLaneForWindow(window);
    const auto targetWindow = lane ? lane->get_active_window() : nullptr;
    const auto targetLaneIndex = laneIndexOf(lane);
    spdlog::info(
        "onWindowFocusChange: window={} workspace={} monitor={} canvas_ws={} lane_found={} lanes={} before_lane={} before_lane_index={} before_window={} target_lane={} target_lane_index={} target_window={} same_lane={}",
        static_cast<const void*>(window.get()),
        window->workspaceID(),
        window->monitorID(),
        CanvasLayoutInternal::get_workspace_id(),
        lane != nullptr,
        totalLanes,
        static_cast<const void*>(beforeLane),
        beforeLaneIndex,
        static_cast<const void*>(beforeWindow ? beforeWindow.get() : nullptr),
        static_cast<const void*>(lane),
        targetLaneIndex,
        static_cast<const void*>(targetWindow ? targetWindow.get() : nullptr),
        beforeLane == lane);

    if (lane == nullptr) {
        spdlog::warn("onWindowFocusChange: window={} not managed by current canvas canvas_ws={} lanes={}",
                     static_cast<const void*>(window.get()),
                     CanvasLayoutInternal::get_workspace_id(),
                     totalLanes);
        return;
    }

    setActiveLane(lane);
    lane->focus_window(window);

    const auto afterLane = activeLane ? activeLane->data() : nullptr;
    const auto afterWindow = afterLane ? afterLane->get_active_window() : nullptr;
    spdlog::info("onWindowFocusChange: synced window={} canvas_ws={} after_lane={} after_lane_index={} after_window={}",
                 static_cast<const void*>(window.get()),
                 CanvasLayoutInternal::get_workspace_id(),
                 static_cast<const void*>(afterLane),
                 laneIndexOf(afterLane),
                 static_cast<const void*>(afterWindow ? afterWindow.get() : nullptr));
    persistSnapshot();
}

bool CanvasLayout::syncSpecialWorkspaceVisibilityState(PHLMONITOR visibleMonitor)
{
    const auto workspace = getCanvasWorkspace();
    const auto currentLane = activeLane ? activeLane->data() : nullptr;
    if (!workspace) {
        specialEphemeralLaneRestorePending = false;
        return false;
    }
    if (!workspace->m_isSpecialWorkspace) {
        specialEphemeralLaneRestorePending = false;
        return false;
    }
    if (!currentLane)
        return false;

    const auto workspaceVisible = visibleMonitor != nullptr;
    // Hidden special workspaces can temporarily leave the canvas pointing at an
    // empty ephemeral lane. Remember that state while hidden so we can re-adopt
    // the real focused lane when the special workspace becomes visible again.
    if (CanvasLayoutInternal::should_mark_special_ephemeral_lane_for_restore(
            workspace->m_isSpecialWorkspace,
            workspaceVisible,
            currentLane->is_ephemeral(),
            currentLane->empty())) {
        specialEphemeralLaneRestorePending = true;
        return false;
    }

    if (workspaceVisible && specialEphemeralLaneRestorePending &&
        (!currentLane->is_ephemeral() || !currentLane->empty())) {
        specialEphemeralLaneRestorePending = false;
        return false;
    }

    if (!CanvasLayoutInternal::should_restore_marked_special_ephemeral_lane(
            workspace->m_isSpecialWorkspace,
            workspaceVisible,
            specialEphemeralLaneRestorePending,
            currentLane->is_ephemeral(),
            currentLane->empty()))
        return false;

    // On reopen, pull the workspace's remembered focused window back into the
    // canvas model before the next relayout reads stale ephemeral-lane state.
    syncActiveStateFromWorkspaceFocus();

    const auto afterLane = activeLane ? activeLane->data() : nullptr;
    const auto restored = !(afterLane && afterLane->is_ephemeral() && afterLane->empty());
    if (restored)
        specialEphemeralLaneRestorePending = false;
    return restored;
}

// Adopt the lane that owns Hyprland's current focused window, dropping a stale
// empty temporary lane when necessary.
bool CanvasLayout::adoptFocusedLane(PHLWINDOW focusedWindow, PHLMONITOR fallbackMonitor)
{
    if (!focusedWindow)
        return false;

    const auto currentLane = activeLane ? activeLane->data() : nullptr;
    const auto currentWindow = currentLane ? currentLane->get_active_window() : nullptr;
    auto targetLane = getLaneForWindow(focusedWindow);
    if (!targetLane)
        return false;

    // If Hyprland focus jumped back onto a real window while the canvas still
    // points at a blank ephemeral lane, discard that temporary lane first so
    // the active-lane pointer reattaches to real model state.
    if (activeLane && currentLane && currentLane->is_ephemeral() && currentLane->empty() && targetLane != currentLane)
        dropEmptyLane(activeLane, targetLane, fallbackMonitor, true);

    if (currentWindow == focusedWindow)
        return true;

    onWindowFocusChange(focusedWindow);
    return true;
}

// Pull remembered focus state from the canvas workspace and mirror it into the
// plugin's active lane/window state.
void CanvasLayout::syncActiveStateFromWorkspaceFocus()
{
    const auto workspace = getCanvasWorkspace();
    if (!workspace)
        return;

    const auto focusedWindow = workspace->getLastFocusedWindow();
    const auto currentLane = activeLane ? activeLane->data() : nullptr;
    const auto currentWindow = currentLane ? currentLane->get_active_window() : nullptr;
    const auto managed = focusedWindow && getLaneForWindow(focusedWindow) != nullptr;

    spdlog::debug("syncActiveStateFromWorkspaceFocus: canvas_ws={} focused_window={} focused_workspace={} focused_monitor={} managed={} current_window={}",
                  workspace->m_id,
                  static_cast<const void*>(focusedWindow ? focusedWindow.get() : nullptr),
                  focusedWindow ? focusedWindow->workspaceID() : WORKSPACE_INVALID,
                  focusedWindow ? focusedWindow->monitorID() : MONITOR_INVALID,
                  managed,
                  static_cast<const void*>(currentWindow ? currentWindow.get() : nullptr));

    if (!managed || currentWindow == focusedWindow)
        return;

    if (adoptFocusedLane(focusedWindow, getVisibleCanvasMonitor()))
        spdlog::info("syncActiveStateFromWorkspaceFocus: adopted focused lane canvas_ws={} focused_window={}",
                     workspace->m_id,
                     static_cast<const void*>(focusedWindow.get()));
}

// Resolve the window and lane that should receive focus after a cross-monitor
// handoff, preferring the destination canvas's active lane before geometry fallback.
PHLWINDOW CanvasLayout::resolveCrossMonitorFocusTarget(CanvasLayout *targetLayout, PHLMONITOR monitor, WORKSPACEID workspaceId, Direction direction, PHLWINDOW sourceWindow, Lane **targetLane, const char **selection)
{
    auto *resolvedLane = targetLayout ? targetLayout->getActiveLane() : nullptr;
    const char *resolvedSelection = "geometry";

    // First try to let the destination canvas re-adopt its remembered focused
    // lane/window. That preserves local intent better than pure geometry-based
    // fallback when the target workspace already has a meaningful active item.
    if (targetLayout)
        targetLayout->syncActiveStateFromWorkspaceFocus();

    resolvedLane = targetLayout ? targetLayout->getActiveLane() : nullptr;
    auto targetWindow = resolvedLane ? resolvedLane->get_active_window() : nullptr;
    if (targetWindow) {
        resolvedSelection = "active";
    } else {
        targetWindow = CanvasLayoutInternal::pick_cross_monitor_target_window(monitor, workspaceId, direction, sourceWindow);
    }

    if (targetLayout && targetWindow && (!resolvedLane || !resolvedLane->has_window(targetWindow)))
        resolvedLane = targetLayout->getLaneForWindow(targetWindow);

    if (targetLane)
        *targetLane = resolvedLane;
    if (selection)
        *selection = resolvedSelection;

    return targetWindow;
}

// Apply destination-canvas state once a cross-monitor target has been chosen.
void CanvasLayout::activateCrossMonitorFocusTarget(CanvasLayout *targetLayout, Lane *targetLane, PHLWINDOW targetWindow, PHLMONITOR fallbackMonitor)
{
    if (!targetLayout || !targetLane || !targetWindow)
        return;

    // Make the destination canvas internally coherent before Hyprland focus is
    // switched. Otherwise subsequent relayout/focus-sync code would still think
    // the old lane is active.
    targetLayout->setActiveLane(targetLane);
    targetLane->focus_window(targetWindow);

    const auto targetWorkspace = targetLayout->getCanvasWorkspace();
    auto targetMonitor = targetWorkspace ? CanvasLayoutInternal::visible_monitor_for_workspace(targetWorkspace) : fallbackMonitor;
    if (targetMonitor)
        targetLayout->relayoutCanvas(targetMonitor, targetWorkspace && !targetWorkspace->m_isSpecialWorkspace);
}

// Execute the whole cross-monitor focus handoff and keep the destination canvas
// state coherent before Hyprland focus is switched.
void CanvasLayout::handoffFocusAcrossMonitor(int workspace, Direction direction, PHLWINDOW sourceWindow, PHLMONITOR sourceMonitor, WORKSPACEID sourceActiveWorkspaceId, WORKSPACEID sourceSpecialWorkspaceId, ListNode<Lane *> *sourceLaneNode, PHLMONITOR targetMonitor)
{
    const auto activeWorkspaceId = targetMonitor ? targetMonitor->activeWorkspaceID() : WORKSPACE_INVALID;
    const auto workspaceId = CanvasLayoutInternal::preferred_workspace_id(targetMonitor, workspace);
    const auto specialWorkspaceId = targetMonitor ? targetMonitor->activeSpecialWorkspaceID() : WORKSPACE_INVALID;
    spdlog::info(
        "move_focus_cross_monitor: source_workspace={} direction={} source_window={} "
        "source_active_ws={} source_special_ws={} dest_active_ws={} dest_special_ws={} selected_ws={}",
        workspace,
        ScrollerCore::direction_name(direction),
        static_cast<const void*>(sourceWindow ? sourceWindow.get() : nullptr),
        sourceActiveWorkspaceId,
        sourceSpecialWorkspaceId,
        activeWorkspaceId,
        specialWorkspaceId,
        workspaceId);
    if (!targetMonitor) {
        spdlog::warn("move_focus: no monitor in direction={} from workspace={}",
                     ScrollerCore::direction_name(direction), workspace);
        return;
    }

    // Phase 1: choose the destination workspace/lane/window.
    const char *targetSelection = "geometry";
    auto *targetLayout = CanvasLayoutInternal::get_canvas_for_workspace(workspaceId);
    auto *targetLane = static_cast<Lane *>(nullptr);
    auto crossMonitorTarget = resolveCrossMonitorFocusTarget(targetLayout, targetMonitor, workspaceId, direction, sourceWindow, &targetLane, &targetSelection);
    // Phase 2: if the user left a blank ephemeral lane behind, clean it up
    // before we hand control to another monitor/workspace.
    if (dropEmptyLane(sourceLaneNode, nullptr, sourceMonitor, true)) {
        spdlog::info("move_focus: dropped empty lane after leaving workspace={} direction={}",
                     workspace, ScrollerCore::direction_name(direction));
    }
    persistSnapshot();

    // Phase 3: either focus an empty target workspace or activate the chosen
    // destination window and let Hyprland follow that focus.
    if (!crossMonitorTarget) {
        const auto targetWorkspace = g_pCompositor->getWorkspaceByID(workspaceId);
        spdlog::info("move_focus: no target window for crossed monitor workspace={} target_monitor={} target_workspace_found={}",
                     workspaceId,
                     targetMonitor->m_id,
                     targetWorkspace != nullptr);
        CanvasLayoutInternal::focus_monitor_workspace(targetMonitor,
                                                      targetWorkspace,
                                                      workspaceId,
                                                      true,
                                                      "move_focus_cross_monitor_empty_target");
        return;
    }

    spdlog::info(
        "move_focus_cross_monitor_target: selected_ws={} target_window={} target_workspace={} "
        "target_layout_found={} target_lane_found={} selection={} target_pos=({}, {}) target_size=({}, {})",
        workspaceId,
        static_cast<const void*>(crossMonitorTarget ? crossMonitorTarget.get() : nullptr),
        crossMonitorTarget ? crossMonitorTarget->workspaceID() : WORKSPACE_INVALID,
        targetLayout != nullptr,
        targetLane != nullptr,
        targetSelection,
        crossMonitorTarget ? crossMonitorTarget->m_position.x : 0.0,
        crossMonitorTarget ? crossMonitorTarget->m_position.y : 0.0,
        crossMonitorTarget ? crossMonitorTarget->m_size.x : 0.0,
        crossMonitorTarget ? crossMonitorTarget->m_size.y : 0.0);

    if (targetLane == nullptr) {
        spdlog::warn("move_focus: no lane for crossed monitor target window={} workspace={}",
                     static_cast<const void*>(crossMonitorTarget.get()), workspaceId);
    } else {
        activateCrossMonitorFocusTarget(targetLayout, targetLane, crossMonitorTarget, targetMonitor);
    }

    spdlog::info("move_focus: workspace={} direction={} after={} result={}",
                 workspace,
                 ScrollerCore::direction_name(direction),
                 static_cast<const void*>(crossMonitorTarget.get()),
                 "cross_monitor");
    focusManagedWindow(crossMonitorTarget, true, "move_focus_cross_monitor", true);
}

// Compatibility entrypoint used by older target-based move-window callbacks.
void CanvasLayout::moveWindowTo(PHLWINDOW window, const std::string &direction, bool)
{
    if (!window || direction.empty()) {
        spdlog::debug("moveWindowTo: ignored invalid request window={} direction_empty={}",
                      static_cast<const void*>(window ? window.get() : nullptr),
                      direction.empty());
        return;
    }

    auto s = getLaneForWindow(window);
    if (s == nullptr || !s->is_active(window))
        return;

    onWindowFocusChange(window);

    switch (direction.front()) {
        case 'l': move_window(window->workspaceID(), Direction::Left); break;
        case 'r': move_window(window->workspaceID(), Direction::Right); break;
        case 'u': move_window(window->workspaceID(), Direction::Up); break;
        case 'd': move_window(window->workspaceID(), Direction::Down); break;
        default: break;
    }
}

void CanvasLayout::focusAdjacentLane(int workspace, Direction direction, ListNode<Lane *> *sourceLaneNode,
                                     PHLMONITOR sourceMonitor, ListNode<Lane *> *targetLaneNode) {
    if (!targetLaneNode)
        return;

    // Adjacent-lane focus is the local-page analogue of cross-monitor handoff:
    // update active-lane state first, then prune any empty source lane, then
    // switch Hyprland focus to the destination lane's active window.
    activeLane = targetLaneNode;
    if (dropEmptyLane(sourceLaneNode, activeLane ? activeLane->data() : nullptr, sourceMonitor, true)) {
        spdlog::info("move_focus: dropped empty lane after leaving workspace={} direction={}",
                     workspace, ScrollerCore::direction_name(direction));
    } else {
        relayoutVisibleCanvas(sourceMonitor);
    }

    const auto targetWindow = activeLane->data()->get_active_window();
    spdlog::info("move_focus: workspace={} direction={} lane_switch={} target_window={}",
                 workspace,
                 ScrollerCore::direction_name(direction),
                 true,
                 static_cast<const void*>(targetWindow ? targetWindow.get() : nullptr));
    if (targetWindow) {
        focusManagedWindow(targetWindow, true, "move_focus_adjacent_lane", true);
    } else {
        persistSnapshot();
    }
}

void CanvasLayout::createEphemeralLaneForFocus(int workspace, Direction direction, PHLMONITOR sourceMonitor,
                                               Mode mode, ListNode<Lane *> *anchor) {
    // Empty lanes are navigation-only placeholders. They let the user "move
    // into blank space" and then create/move content there later without
    // immediately mutating existing real lanes.
    auto *newLane = new Lane(sourceMonitor, mode);
    newLane->set_ephemeral(true);
    auto newLaneNode = insertLaneNode(newLane, direction, anchor);

    activeLane = newLaneNode;
    relayoutVisibleCanvas(sourceMonitor);
    spdlog::info("move_focus: created empty lane workspace={} direction={} lane={}",
                 workspace,
                 ScrollerCore::direction_name(direction),
                 static_cast<const void*>(newLane));
    persistSnapshot();
}

void CanvasLayout::finalizeLocalFocusMove(int workspace, Direction direction, Lane *lane,
                                          const char *moveResultName) {
    if (!lane)
        return;

    const auto after = lane->get_active_window();
    spdlog::info("move_focus: workspace={} direction={} after={} result={}",
                 workspace,
                 ScrollerCore::direction_name(direction),
                 static_cast<const void*>(after ? after.get() : nullptr),
                 moveResultName);

    // Keep the plugin's active-lane pointer in sync before handing focus back
    // to Hyprland. Command code assumes the model already points at the same
    // lane/window Hyprland is about to focus.
    setActiveLane(lane);
    focusManagedWindow(after, true, "move_focus_local", true);
}

// Execute directional focus movement, including lane handoff, monitor handoff,
// and temporary empty-lane creation when the user moves into blank space.
// Reading guide:
// `dispatch_movefocus` resolves the active `CanvasLayout` and lands here.
// This function then turns one directional request into one of a few routes:
// local focus move, adjacent-lane focus, cross-monitor handoff, or creation of
// a temporary empty lane that the user can move into.
void CanvasLayout::move_focus(int workspace, Direction direction)
{
    // Phase 1: snapshot the current lane/window/monitor state before any move.
    // Both local focus changes and cross-monitor handoff decisions depend on
    // the same source state, so keep that snapshot stable for the whole route.
    if (CanvasLayoutInternal::should_sync_workspace_focus_before_move(activeLane))
        syncActiveStateFromWorkspaceFocus();
    auto lane = getActiveLane();
    const auto before = lane ? lane->get_active_window() : nullptr;
    const auto activeCanvasMonitor = getVisibleCanvasMonitor();
    const auto sourceMonitor = before ? g_pCompositor->getMonitorFromID(before->monitorID())
                                     : (activeCanvasMonitor ? activeCanvasMonitor : ScrollerCore::monitorFromPointingOrCursor());
    const auto beforeActiveWorkspaceId = sourceMonitor ? sourceMonitor->activeWorkspaceID() : WORKSPACE_INVALID;
    const auto beforeSpecialWorkspaceId = sourceMonitor ? sourceMonitor->activeSpecialWorkspaceID() : WORKSPACE_INVALID;
    auto sourceLaneNode = activeLane;
    spdlog::info("move_focus: workspace={} direction={} lane_found={} before={}",
                 workspace, ScrollerCore::direction_name(direction), lane != nullptr,
                 static_cast<const void*>(before ? before.get() : nullptr));
    if (lane == nullptr) {
        CanvasLayoutInternal::dispatch_builtin_movefocus(direction);
        return;
    }

    const auto mode = lane->get_mode();
    const auto betweenLanes = CanvasLayoutInternal::direction_moves_between_lanes(mode, direction);
    const auto laneEmpty = lane->empty();
    const auto targetMonitorFromDirection = directionalMoveTargetMonitor(sourceMonitor, direction);
    const auto emptyLaneTargetMonitor = laneEmpty
        ? targetMonitorFromDirection
        : nullptr;
    const auto moveResult = laneEmpty ? FocusMoveResult::NoOp : lane->move_focus(direction, scroller::plugin_config::focusWrap());
    if (CanvasLayoutInternal::should_cross_monitor_from_empty_lane(laneEmpty, betweenLanes, emptyLaneTargetMonitor != nullptr)) {
        handoffFocusAcrossMonitor(workspace,
                                  direction,
                                  before,
                                  sourceMonitor,
                                  beforeActiveWorkspaceId,
                                  beforeSpecialWorkspaceId,
                                  sourceLaneNode,
                                  emptyLaneTargetMonitor);
        return;
    }

    if (!laneEmpty && !betweenLanes && moveResult == FocusMoveResult::NoOp && targetMonitorFromDirection) {
        spdlog::info("move_focus: blocked local move, attempting cross-monitor fallback workspace={} direction={}",
                     workspace, ScrollerCore::direction_name(direction));
        handoffFocusAcrossMonitor(workspace,
                                  direction,
                                  before,
                                  sourceMonitor,
                                  beforeActiveWorkspaceId,
                                  beforeSpecialWorkspaceId,
                                  sourceLaneNode,
                                  targetMonitorFromDirection);
        return;
    }

    // Phase 2: lane-axis routing stays pure until the final action dispatch so
    // the empty-lane, adjacent-lane, and cross-monitor cases remain readable
    // and unit-testable.
    const auto handoffPlan = betweenLanes
        ? CanvasLayoutInternal::plan_directional_handoff(
              lanes, activeLane, sourceMonitor, mode, direction, !laneEmpty, CanvasLayoutInternal::resolve_monitor_in_direction)
        : CanvasLayoutInternal::DirectionalHandoffPlan{};
    const auto action = CanvasLayoutInternal::decide_move_focus_route(true, laneEmpty, betweenLanes, moveResult, handoffPlan.route);

    switch (action) {
    case CanvasLayoutInternal::MoveFocusRouteAction::DispatchBuiltin:
        CanvasLayoutInternal::dispatch_builtin_movefocus(direction);
        return;
    case CanvasLayoutInternal::MoveFocusRouteAction::NoOp:
        return;
    case CanvasLayoutInternal::MoveFocusRouteAction::AdjacentLane:
        focusAdjacentLane(workspace, direction, sourceLaneNode, sourceMonitor, handoffPlan.targetLaneNode);
        return;
    case CanvasLayoutInternal::MoveFocusRouteAction::CrossMonitor: {
        const auto monitor = moveResult == FocusMoveResult::CrossMonitor
            ? CanvasLayoutInternal::resolve_monitor_in_direction(sourceMonitor, direction)
            : handoffPlan.targetMonitor;
        handoffFocusAcrossMonitor(workspace, direction, before, sourceMonitor, beforeActiveWorkspaceId, beforeSpecialWorkspaceId, sourceLaneNode, monitor);
        return;
    }
    case CanvasLayoutInternal::MoveFocusRouteAction::CreateLane:
        createEphemeralLaneForFocus(workspace, direction, sourceMonitor, mode, handoffPlan.targetLaneNode);
        return;
    case CanvasLayoutInternal::MoveFocusRouteAction::FinalizeLocalMove:
        finalizeLocalFocusMove(workspace, direction, lane, focus_move_result_name(moveResult));
        return;
    }
}
