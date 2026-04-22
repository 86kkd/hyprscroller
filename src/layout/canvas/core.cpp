/**
 * @file core.cpp
 * @brief Canvas lifecycle, lane ownership, and Hyprland tiled-algorithm glue.
 *
 * This file owns the non-directional core of `CanvasLayout`: lane list
 * management, relayout of the whole canvas, Hyprland target callbacks, and
 * removal/creation flows that keep canvas state coherent.
 *
 * Reading guide for new contributors:
 * - `layout.h` explains the ownership hierarchy (`CanvasLayout` -> `Lane` -> `Stack`)
 * - this file answers "who owns what and when do we relayout?"
 * - `focus.cpp` answers "where should focus move next?"
 *
 * In practice, this file is the state-maintenance layer around the model:
 * - it owns the ordered lane list for one workspace
 * - it keeps cache structures in sync with that list
 * - it attaches/detaches workspace listeners
 * - it translates monitor/workspace changes into lane relayout calls
 */
#include <algorithm>
#include <cassert>

#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/config/ConfigManager.hpp>
#include <hyprland/src/config/ConfigValue.hpp>
#include <hyprland/src/event/EventBus.hpp>
#include <hyprland/src/helpers/Monitor.hpp>
#include <hyprland/src/layout/algorithm/Algorithm.hpp>
#include <hyprland/src/layout/space/Space.hpp>
#include <spdlog/spdlog.h>

#include "../../core/core.h"
#include "../../core/layout_profile.h"
#include "../../core/monitor_geometry_runtime.h"
#include "../../core/window_key.h"
#include "../lane/lane.h"
#include "layout.h"
#include "internal.h"
#include "route.h"

using namespace ScrollerCore;

// Global mark registry shared by all canvas instances.
static Marks marks;

namespace {
// Destroy and clear all lanes owned by a canvas instance.
void clear_lanes(List<Lane*>& lanes) {
    for (auto lane = lanes.first(); lane != nullptr; lane = lane->next())
        delete lane->data();
    lanes.clear();
}

// Return true when any lane is a temporary page-like lane.
} // namespace

CanvasLayout::CanvasLayout() {
    // Keep canvas-local active-lane state aligned with Hyprland's focused
    // window. `resetRuntimeState` can tear this listener down when a canvas is
    // detached from a workspace and later rebuild it on demand.
    m_focusCallback = Event::bus()->m_events.window.active.listen([this](PHLWINDOW window, Desktop::eFocusReason) {
        onWindowFocusChange(window);
    });
}

CanvasLayout::~CanvasLayout() {
    resetRuntimeState();
}

void CanvasLayout::resetRuntimeState() {
    // Drop every workspace-bound runtime seam. This is the "canvas instance is
    // no longer attached to a live workspace" cleanup path, so it must clear
    // both listeners and all cached ownership/handoff state.
    m_focusCallback = nullptr;
    m_workspaceActiveCallback = nullptr;
    workspaceRuntimeId = WORKSPACE_INVALID;
    clear_lanes(lanes);
    activeLane = nullptr;
    laneByWindow.clear();
    resetHandoffState();
    specialEphemeralLaneRestorePending = false;
}

void CanvasLayout::ensureWorkspaceRuntime() {
    // A `CanvasLayout` can outlive the concrete workspace it is currently bound
    // to. Rebuild listeners lazily so command paths can safely call this before
    // touching workspace-bound state.
    if (!m_focusCallback) {
        m_focusCallback = Event::bus()->m_events.window.active.listen([this](PHLWINDOW window, Desktop::eFocusReason) {
            onWindowFocusChange(window);
        });
    }

    const auto workspace = getCanvasWorkspace();
    if (!workspace) {
        m_workspaceActiveCallback = nullptr;
        workspaceRuntimeId = WORKSPACE_INVALID;
        return;
    }

    if (workspaceRuntimeId == workspace->m_id && m_workspaceActiveCallback)
        return;

    workspaceRuntimeId = workspace->m_id;
    m_workspaceActiveCallback = workspace->m_events.activeChanged.listen([this] {
        // Special workspaces can disappear without their hidden canvas ticking.
        // When the active state flips, resync hidden-canvas bookkeeping first,
        // then relayout if the visibility transition changed the lane state.
        const auto workspace = getCanvasWorkspace();
        if (!workspace)
            return;
        syncHiddenSpecialWorkspaceCanvases();
        const auto monitor = CanvasLayoutInternal::visible_monitor_for_workspace(workspace);
        if (syncSpecialWorkspaceVisibilityState(monitor) && monitor)
            relayoutCanvas(monitor, !workspace->m_isSpecialWorkspace);
    });
}

CanvasLayoutInternal::CanvasBounds CanvasLayoutInternal::compute_canvas_bounds(PHLMONITOR monitor) {
    static auto PGAPSINDATA = CConfigValue<Hyprlang::CUSTOMTYPE>("general:gaps_in");
    static auto PGAPSOUTDATA = CConfigValue<Hyprlang::CUSTOMTYPE>("general:gaps_out");
    auto *const PGAPSIN = (CCssGapData *)(PGAPSINDATA.ptr())->getData();
    auto *const PGAPSOUT = (CCssGapData *)(PGAPSOUTDATA.ptr())->getData();

    // Scroller uses one shared interpretation of monitor space. `full` is the
    // logical monitor rectangle and `max` is the workarea after reserved areas
    // and outer gaps are applied. Every lane relayout path should go through
    // this helper so lane and stack code agree on the same coordinate space.
    const auto gaps_in = PGAPSIN->m_top;
    const auto gaps_out = PGAPSOUT->m_top;

    return {
        .full = ScrollerCore::logical_monitor_box(monitor),
        .max = ScrollerCore::logical_workarea_box(monitor, gaps_out),
        .gap = static_cast<int>(gaps_in),
    };
}

PHLWORKSPACE CanvasLayout::getCanvasWorkspace() const {
    const auto algorithm = m_parent.lock();
    const auto space = algorithm ? algorithm->space() : nullptr;
    return space ? space->workspace() : nullptr;
}

Lane *CanvasLayout::getActiveLane() {
    // New readers often expect "no active lane" to remain null. In practice we
    // default to the first lane so command paths have a stable current lane as
    // soon as the canvas contains anything.
    if (activeLane)
        return activeLane->data();
    if (lanes.first()) {
        activeLane = lanes.first();
        return activeLane->data();
    }
    return nullptr;
}

ListNode<Lane *> *CanvasLayout::getLaneNode(Lane *lane) const {
    if (!lane)
        return nullptr;

    for (auto node = lanes.first(); node != nullptr; node = node->next()) {
        if (node->data() == lane)
            return node;
    }

    return nullptr;
}

int CanvasLayout::laneIndexOf(Lane *lane) const {
    auto index = 0;
    for (auto node = lanes.first(); node != nullptr; node = node->next(), ++index) {
        if (node->data() == lane)
            return index;
    }

    return -1;
}

size_t CanvasLayout::laneCount() const {
    size_t count = 0;
    for (auto node = lanes.first(); node != nullptr; node = node->next(), ++count) { }
    return count;
}

void CanvasLayout::setActiveLane(Lane *lane) {
    activeLane = getLaneNode(lane);
}

void CanvasLayout::rememberWindowLane(PHLWINDOW window, Lane *lane) {
    if (!window) {
        return;
    }

    if (!lane) {
        forgetWindowLane(window);
        return;
    }

    // The owner index is an optimization only. Every write path still has to
    // treat it as a cache and keep it synchronized with the authoritative lane
    // list.
    laneByWindow.remember(ScrollerCore::window_key(window), lane);
}

void CanvasLayout::forgetWindowLane(PHLWINDOW window) {
    if (!window)
        return;

    laneByWindow.forget(ScrollerCore::window_key(window));
}

void CanvasLayout::rememberLaneWindows(Lane *lane) {
    if (!lane)
        return;

    laneByWindow.remember_owner(lane, [&](auto &&remember) {
        lane->for_each_window([&](PHLWINDOW window) {
            remember(ScrollerCore::window_key(window));
        });
    });
}

void CanvasLayout::forgetLaneWindows(Lane *lane) {
    if (!lane)
        return;

    laneByWindow.forget_owner(lane);
}

void CanvasLayout::debugVerifyLaneCache() const {
#ifndef NDEBUG
    assert(laneByWindow.matches_expected([&](auto &&addExpected) {
        for (auto laneNode = lanes.first(); laneNode != nullptr; laneNode = laneNode->next()) {
            auto *lane = laneNode->data();
            lane->for_each_window([&](PHLWINDOW window) {
                addExpected(ScrollerCore::window_key(window), lane);
            });
        }
    }));
#endif
}

ListNode<Lane *> *CanvasLayout::insertLaneNode(Lane *lane, Direction direction, ListNode<Lane *> *anchor) {
    if (!lane)
        return nullptr;

    lanes.push_back(lane);
    auto node = lanes.last();
    if (!anchor || anchor == node)
        return node;

    // Direction semantics are orientation-aware. "insert left" and "insert up"
    // both mean "before current lane" once the lane mode is resolved.
    if (CanvasLayoutInternal::direction_inserts_before_current(lane->get_mode(), direction))
        lanes.move_before(anchor, node);
    else
        lanes.move_after(anchor, node);

    return node;
}

Lane *CanvasLayout::ensureActiveLane(PHLMONITOR monitor, Mode mode) {
    if (auto *lane = getActiveLane())
        return lane;

    auto *lane = new Lane(monitor, mode);
    activeLane = insertLaneNode(lane, Direction::End);
    return lane;
}

void CanvasLayout::rememberManualCrossMonitorInsertion(PHLWINDOW window) {
    if (!window)
        return;

    handoffState.rememberManualCrossMonitorInsertion(ScrollerCore::window_key(window));
}

void CanvasLayout::forgetManualCrossMonitorInsertion(PHLWINDOW window) {
    if (!window)
        return;

    handoffState.forgetManualCrossMonitorInsertion(ScrollerCore::window_key(window));
}

bool CanvasLayout::hasPendingManualCrossMonitorInsertion(PHLWINDOW window) const {
    return window && handoffState.hasPendingManualCrossMonitorInsertion(ScrollerCore::window_key(window));
}

void CanvasLayout::requestWorkspaceFocusSyncSuppression() {
    handoffState.requestWorkspaceFocusSyncSuppression();
}

ActiveLaneSyncPolicy CanvasLayout::consumeActiveLaneSyncPolicy() {
    return handoffState.consumeActiveLaneSyncPolicy();
}

void CanvasLayout::resetHandoffState() {
    handoffState.reset();
}

bool CanvasLayout::focusManagedWindow(PHLWINDOW window, bool warpCursor, const char *context, bool suppressWorkspaceSync) {
    if (!window)
        return false;

    if (suppressWorkspaceSync)
        requestWorkspaceFocusSyncSuppression();

    if (CanvasLayoutInternal::switch_to_window(window, warpCursor))
        return true;

    if (suppressWorkspaceSync)
        (void)consumeActiveLaneSyncPolicy();

    const auto *ctx = context ? context : "focusManagedWindow";
    spdlog::warn("{}: failed to focus managed window canvas_ws={} window={} target_workspace={} target_monitor={}",
                 ctx,
                 CanvasLayoutInternal::get_workspace_id(),
                 static_cast<const void*>(window.get()),
                 window->workspaceID(),
                 window->monitorID());
    syncActiveStateFromWorkspaceFocus();
    return false;
}

void CanvasLayout::finishLaneTransfer(ListNode<Lane *> *sourceLaneNode, PHLMONITOR sourceMonitor, bool ephemeralOnly, bool warpCursor) {
    // Payload transfer helpers in focus/move-window paths call this as the last
    // phase: prune an empty source lane if needed, relayout the visible canvas,
    // then let Hyprland focus follow the lane's new active window.
    if (!dropEmptyLane(sourceLaneNode, activeLane ? activeLane->data() : nullptr, sourceMonitor, ephemeralOnly))
        relayoutVisibleCanvas(sourceMonitor);

    if (const auto lane = getActiveLane()) {
        if (const auto window = lane->get_active_window())
            focusManagedWindow(window, warpCursor, "finishLaneTransfer");
    }
}

Lane *CanvasLayout::getLaneForWindow(PHLWINDOW window) {
    if (!window)
        return nullptr;

    // This is the lane-level sibling of `Lane::getStackForWindow`: consult the
    // cache first, validate the cached owner, then fall back to a full scan.
    const auto key = ScrollerCore::window_key(window);
    if (auto *cachedLane = laneByWindow.find_valid(key, [&](Lane *owner) {
            return owner && getLaneNode(owner) && owner->has_window(window);
        }))
        return cachedLane;

    for (auto lane = lanes.first(); lane != nullptr; lane = lane->next()) {
        if (lane->data()->has_window(window)) {
            rememberWindowLane(window, lane->data());
            return lane->data();
        }
    }
    return nullptr;
}

// Resolve the monitor currently displaying this canvas.
PHLMONITOR CanvasLayout::getVisibleCanvasMonitor(PHLMONITOR fallbackMonitor) const {
    const auto workspace = getCanvasWorkspace();
    if (!workspace)
        return fallbackMonitor;

    const auto visibleMonitor = CanvasLayoutInternal::visible_monitor_for_workspace(workspace);
    return visibleMonitor ? visibleMonitor : fallbackMonitor;
}

void CanvasLayout::prepareForActionContext() {
    // Dispatcher-facing entrypoints call this before mutating the canvas. It is
    // the "make sure hidden special workspaces and listeners are not stale"
    // checkpoint shared by command code.
    ensureWorkspaceRuntime();

    syncHiddenSpecialWorkspaceCanvases();
    const auto workspace = getCanvasWorkspace();
    const auto monitor = getVisibleCanvasMonitor();
    if (syncSpecialWorkspaceVisibilityState(monitor) && workspace && monitor)
        relayoutCanvas(monitor, !workspace->m_isSpecialWorkspace);
}

// Relayout the canvas on the monitor currently showing it.
void CanvasLayout::relayoutVisibleCanvas(PHLMONITOR fallbackMonitor) {
    const auto workspace = getCanvasWorkspace();
    const auto monitor = getVisibleCanvasMonitor(fallbackMonitor);
    if (workspace && monitor)
        relayoutCanvas(monitor, !workspace->m_isSpecialWorkspace);
}

// Remove an empty lane and repair active-lane state around the removal point.
bool CanvasLayout::dropEmptyLane(ListNode<Lane *> *laneNode, Lane *preferredLane, PHLMONITOR fallbackMonitor, bool ephemeralOnly) {
    if (!laneNode || !laneNode->data())
        return false;

    auto *lane = laneNode->data();
    if (ephemeralOnly && !lane->is_ephemeral())
        return false;
    if (!lane->empty())
        return false;

    // Once a lane is removed we must still leave the canvas pointing at a valid
    // active lane. Prefer the explicit caller choice, otherwise pick an
    // adjacent lane when the removed lane used to be active.
    Lane *fallbackLane = preferredLane;
    if (!fallbackLane && activeLane == laneNode) {
        if (laneNode->next())
            fallbackLane = laneNode->next()->data();
        else if (laneNode->prev())
            fallbackLane = laneNode->prev()->data();
    }

    lanes.erase(laneNode);
    forgetLaneWindows(lane);
    delete lane;
    setActiveLane(fallbackLane);
    relayoutVisibleCanvas(fallbackMonitor);
    debugVerifyLaneCache();
    return true;
}

// Compatibility wrapper for call sites that only want to drop temporary lanes.
bool CanvasLayout::dropEmptyEphemeralLane(ListNode<Lane *> *laneNode, Lane *preferredLane, PHLMONITOR fallbackMonitor) {
    return dropEmptyLane(laneNode, preferredLane, fallbackMonitor, true);
}

// Choose the lane that should become active after a lane was removed.
Lane *CanvasLayout::resolveActiveLaneAfterRemoval(ListNode<Lane *> *laneNode, PHLWINDOW removedWindow) {
    const auto workspaceHandle = getCanvasWorkspace();
    if (workspaceHandle) {
        const auto focusedWindow = workspaceHandle->getLastFocusedWindow();
        if (focusedWindow && focusedWindow != removedWindow) {
            if (auto *preferredLane = getLaneForWindow(focusedWindow))
                return preferredLane;
        }
    }

    if (activeLane == laneNode) {
        if (laneNode->next())
            return laneNode->next()->data();
        if (laneNode->prev())
            return laneNode->prev()->data();
        return nullptr;
    }

    return activeLane ? activeLane->data() : nullptr;
}

// Recalculate every lane inside this canvas against one visible monitor.
void CanvasLayout::relayoutCanvas(PHLMONITOR monitor, bool honor_fullscreen) {
    const auto workspace = getCanvasWorkspace();
    if (!workspace || !monitor || lanes.empty())
        return;

    // Single-lane canvases delegate the whole monitor to one lane. Multi-lane
    // canvases instead treat lanes like pages inside a larger logical strip.
    if (lanes.size() == 1) {
        CanvasLayoutInternal::recalculate_workspace_lane(lanes.first()->data(), monitor, workspace, honor_fullscreen);
        return;
    }

    const auto bounds = CanvasLayoutInternal::compute_canvas_bounds(monitor);
    const auto& full = bounds.full;
    const auto& max = bounds.max;

    const auto mode = getActiveLane() ? getActiveLane()->get_mode() : Mode::Row;
    // Lanes represent pages on the canvas. Once a canvas has more than one lane,
    // keep each lane at full workarea size and page between them instead of
    // splitting the monitor into shorter visible rows/columns.
    const auto paged = lanes.size() > 1;
    const auto count = static_cast<double>(lanes.size());
    const auto activeIndex = static_cast<size_t>(std::max(0, laneIndexOf(activeLane ? activeLane->data() : lanes.first()->data())));
    size_t index = 0;
    for (auto lane = lanes.first(); lane != nullptr; lane = lane->next(), ++index) {
        Box laneBox = max;
        if (paged) {
            // Page-mode keeps every lane full-sized and offsets it relative to
            // the active lane. Only one page is visible at a time; the others
            // stay laid out off-screen so focus/move commands can page between
            // them predictably.
            const auto delta = static_cast<double>(index) - static_cast<double>(activeIndex);
            if (ScrollerCore::mode_pages_lanes_vertically(mode))
                laneBox = Box(max.x, max.y + delta * full.h, max.w, max.h);
            else
                laneBox = Box(max.x + delta * full.w, max.y, max.w, max.h);
        } else if (ScrollerCore::mode_pages_lanes_vertically(mode)) {
            const auto unit = max.h / count;
            const auto y = max.y + unit * index;
            const auto h = index + 1 == lanes.size() ? max.y + max.h - y : unit;
            laneBox = Box(max.x, y, max.w, h);
        } else {
            const auto unit = max.w / count;
            const auto x = max.x + unit * index;
            const auto w = index + 1 == lanes.size() ? max.x + max.w - x : unit;
            laneBox = Box(x, max.y, w, max.h);
        }

        lane->data()->set_canvas_geometry(full, laneBox, bounds.gap);
        lane->data()->recalculate_lane_geometry();
    }
}

// Hyprland callback: add a new tiled target into the current canvas.
// New reader map:
// Hyprland knows a window now belongs to the `scroller` layout, so it calls
// this target hook. From here scroller converts the compositor target into its
// own model by forwarding into `onWindowCreatedTiling`.
void CanvasLayout::newTarget(SP<Layout::ITarget> target) {
    ensureWorkspaceRuntime();

    if (!target)
        return;

    auto window = target->window();
    if (!window)
        return;

    if (hasPendingManualCrossMonitorInsertion(window)) {
        spdlog::debug("newTarget: deferring manual cross-monitor registration window={} workspace={}",
                      static_cast<const void*>(window.get()),
                      window->workspaceID());
        return;
    }

    spdlog::info("newTarget: window={} workspace={}", static_cast<const void*>(window.get()), window->workspaceID());
    onWindowCreatedTiling(window, Math::DIRECTION_DEFAULT);
    focusManagedWindow(window, false, "newTarget");
}

// Hyprland callback: target re-entered tiling flow and should be owned again.
void CanvasLayout::movedTarget(SP<Layout::ITarget> target, std::optional<Vector2D>)
{
    ensureWorkspaceRuntime();

    if (!target)
        return;

    auto window = target->window();
    if (!window)
        return;

    if (hasPendingManualCrossMonitorInsertion(window)) {
        spdlog::debug("movedTarget: deferring manual cross-monitor registration window={} workspace={}",
                      static_cast<const void*>(window.get()),
                      window->workspaceID());
        return;
    }

    onWindowCreatedTiling(window, Math::DIRECTION_DEFAULT);
}

// Hyprland callback: remove a tiled target from canvas ownership.
void CanvasLayout::removeTarget(SP<Layout::ITarget> target)
{
    ensureWorkspaceRuntime();

    if (!target)
        return;

    onWindowRemovedTiling(target->window());
}

// Hyprland callback: resize the active window inside the owning lane.
void CanvasLayout::resizeTarget(const Vector2D &delta, SP<Layout::ITarget> target, Layout::eRectCorner)
{
    auto window = windowFromTarget(target);
    if (!window)
        return;

    auto lane = getLaneForWindow(window);
    if (lane == nullptr) {
        if (window->m_realSize)
            *window->m_realSize = Vector2D(std::max((window->m_realSize->goal() + delta).x, 20.0), std::max((window->m_realSize->goal() + delta).y, 20.0));
        window->updateWindowDecos();
        return;
    }

    lane->focus_window(window);
    lane->resize_active_window(delta);
}

// Hyprland callback: relayout the whole canvas after monitor/workspace changes.
void CanvasLayout::recalculate()
{
    ensureWorkspaceRuntime();

    const auto workspace = getCanvasWorkspace();
    if (!workspace)
        return;

    syncHiddenSpecialWorkspaceCanvases();
    const auto monitor = getVisibleCanvasMonitor();
    (void)syncSpecialWorkspaceVisibilityState(monitor);
    if (!monitor)
        return;

    relayoutCanvas(monitor, true);
}

// Explicitly reject layout messages until the plugin defines a supported protocol.
std::expected<void, std::string> CanvasLayout::layoutMsg(const std::string_view& message)
{
    spdlog::warn("layoutMsg: unsupported message='{}'", message);
    return std::unexpected("layout messages are not supported");
}

// Predict the size of a new tiled target using the active lane if present.
std::optional<Vector2D> CanvasLayout::predictSizeForNewTarget()
{
    auto monitor = monitorFromPointingOrCursor();
    if (!monitor)
        return {};

    auto lane = getActiveLane();
    if (!lane) {
        const auto bounds = CanvasLayoutInternal::compute_canvas_bounds(monitor);
        return Vector2D(bounds.max.w, bounds.max.h);
    }

    return lane->predict_window_size();
}

// Return the next target candidate using the active window of the active lane.
SP<Layout::ITarget> CanvasLayout::getNextCandidate(SP<Layout::ITarget> /*old*/)
{
    auto lane = getActiveLane();
    if (!lane)
        return {};

    const auto active = lane->get_active_window();
    if (!active)
        return {};

    return active->layoutTarget();
}

// Swap two targets when they live inside the same lane/stack context.
void CanvasLayout::swapTargets(SP<Layout::ITarget> a, SP<Layout::ITarget> b)
{
    auto wa = windowFromTarget(a);
    auto wb = windowFromTarget(b);
    auto sa = getLaneForWindow(wa);
    auto sb = getLaneForWindow(wb);
    if (!wa || !wb || !sa || !sb || sa != sb)
        return;

    sa->swapWindows(wa, wb);
}

// Hyprland target-level move entrypoint reused by drag/move style operations.
void CanvasLayout::moveTargetInDirection(SP<Layout::ITarget> t, Math::eDirection direction, bool)
{
    auto window = windowFromTarget(t);
    auto s = getLaneForWindow(window);
    if (!s || !window)
        return;

    switch (direction) {
        case Math::DIRECTION_LEFT:
            onWindowFocusChange(window);
            move_window(window->workspaceID(), Direction::Left);
            break;
        case Math::DIRECTION_RIGHT:
            onWindowFocusChange(window);
            move_window(window->workspaceID(), Direction::Right);
            break;
        case Math::DIRECTION_UP:
            onWindowFocusChange(window);
            move_window(window->workspaceID(), Direction::Up);
            break;
        case Math::DIRECTION_DOWN:
            onWindowFocusChange(window);
            move_window(window->workspaceID(), Direction::Down);
            break;
        default:
            return;
    }
}

void CanvasLayout::switchWindows(PHLWINDOW a, PHLWINDOW b)
{
    auto *laneA = getLaneForWindow(a);
    auto *laneB = getLaneForWindow(b);

    if (a) {
        if (laneB)
            rememberWindowLane(a, laneB);
        else
            forgetWindowLane(a);
    }

    if (b) {
        if (laneA)
            rememberWindowLane(b, laneA);
        else
            forgetWindowLane(b);
    }

    debugVerifyLaneCache();
}

// Insert a newly mapped tiled window into the active lane, creating one if
// needed.
// This is the main "window joins the model" step:
// 1. reject duplicates if the window is already owned
// 2. find or create the active lane for this canvas
// 3. let the lane decide where the new window goes locally
// 4. update the window -> lane cache used by later focus/move operations
void CanvasLayout::onWindowCreatedTiling(PHLWINDOW window, Math::eDirection)
{
    if (!window)
        return;

    if (getLaneForWindow(window) != nullptr) {
        spdlog::debug("onWindowCreatedTiling: window already managed window={} workspace={}",
                      static_cast<const void*>(window.get()),
                      window->workspaceID());
        return;
    }

    auto lane = getActiveLane();
    if (lane == nullptr) {
        lane = new Lane(window);
        activeLane = insertLaneNode(lane, Direction::End);
    }
    lane->add_active_window(window);
    rememberWindowLane(window, lane);
    debugVerifyLaneCache();
}

// Remove a tiled window and delete the lane if it becomes empty.
void CanvasLayout::onWindowRemovedTiling(PHLWINDOW window)
{
    const auto windowPtr = static_cast<const void*>(window.get());
    const auto workspace = window ? window->workspaceID() : WORKSPACE_INVALID;
    spdlog::info("onWindowRemovedTiling: window={} workspace={}", windowPtr, workspace);

    marks.remove(window);

    auto s = getLaneForWindow(window);
    if (s == nullptr) {
        spdlog::debug("onWindowRemovedTiling: no lane found for window={} workspace={}", windowPtr, workspace);
        return;
    }

    forgetWindowLane(window);
    if (s->remove_window(window)) {
        debugVerifyLaneCache();
        return;
    }

    auto lane = getLaneNode(s);
    if (!lane) {
        spdlog::warn("onWindowRemovedTiling: empty lane missing from list lane={} workspace={}",
                     static_cast<const void*>(s), workspace);
        return;
    }

    auto *nextActiveLane = resolveActiveLaneAfterRemoval(lane, window);

    auto doomed = lane->data();
    spdlog::info("onWindowRemovedTiling: deleting empty lane={} workspace={}",
                 static_cast<const void*>(doomed), workspace);
    lanes.erase(lane);
    forgetLaneWindows(doomed);
    delete doomed;

    setActiveLane(nextActiveLane);
    relayoutVisibleCanvas();
    debugVerifyLaneCache();
}

// Return whether this canvas currently manages a given window.
bool CanvasLayout::isWindowTiled(PHLWINDOW window)
{
    return getLaneForWindow(window) != nullptr;
}

// Recalculate only the lane that owns a given window.
void CanvasLayout::recalculateWindow(PHLWINDOW window)
{
    auto lane = getLaneForWindow(window);
    if (lane == nullptr)
        return;

    lane->recalculate_lane_geometry();
}

void CanvasLayout::resizeActiveWindow(PHLWINDOW window, const Vector2D &delta,
                                        Layout::eRectCorner, PHLWINDOW pWindow)
{
    const auto PWINDOW = pWindow ? pWindow : window;
    if (!PWINDOW)
        return;

    auto lane = getLaneForWindow(PWINDOW);
    if (lane == nullptr) {
        if (PWINDOW->m_realSize)
            *PWINDOW->m_realSize = Vector2D(std::max((PWINDOW->m_realSize->goal() + delta).x, 20.0), std::max((PWINDOW->m_realSize->goal() + delta).y, 20.0));
        PWINDOW->updateWindowDecos();
        return;
    }

    lane->resize_active_window(delta);
}

void CanvasLayout::alterSplitRatio(PHLWINDOW, float, bool)
{
}

Vector2D CanvasLayout::predictSizeForNewWindowTiled() {
    ensureWorkspaceRuntime();

    auto monitor = monitorFromPointingOrCursor();
    if (!monitor)
        return {};

    auto lane = getActiveLane();
    if (lane == nullptr) {
        const auto bounds = CanvasLayoutInternal::compute_canvas_bounds(monitor);
        return Vector2D(bounds.max.w, bounds.max.h);
    }

    return lane->predict_window_size();
}

void CanvasLayout::replaceWindowDataWith(PHLWINDOW from, PHLWINDOW to)
{
    if (!from || !to)
        return;

    auto *lane = getLaneForWindow(from);
    if (!lane)
        return;

    forgetWindowLane(from);
    rememberWindowLane(to, lane);
    debugVerifyLaneCache();
}

void CanvasLayout::marks_add(const std::string &name) {
    auto lane = getActiveLane();
    if (!lane)
        return;

    PHLWINDOW w = lane->get_active_window();
    if (!w)
        return;

    marks.add(w, name);
}

void CanvasLayout::marks_delete(const std::string &name) {
    marks.del(name);
}

void CanvasLayout::marks_visit(const std::string &name) {
    PHLWINDOW window = marks.visit(name);
    if (window != nullptr)
        focusManagedWindow(window, false, "marks_visit");
}

void CanvasLayout::marks_reset() {
    marks.reset();
}
