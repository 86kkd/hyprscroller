/**
 * @file core.cpp
 * @brief Canvas lifecycle, lane ownership, and Hyprland tiled-algorithm glue.
 *
 * This file owns the non-directional core of `CanvasLayout`: lane list
 * management, relayout of the whole canvas, Hyprland target callbacks, and
 * removal/creation flows that keep canvas state coherent.
 */
#include <algorithm>

#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/config/ConfigManager.hpp>
#include <hyprland/src/config/ConfigValue.hpp>
#include <hyprland/src/event/EventBus.hpp>
#include <hyprland/src/helpers/Monitor.hpp>
#include <hyprland/src/layout/algorithm/Algorithm.hpp>
#include <hyprland/src/layout/space/Space.hpp>
#include <spdlog/spdlog.h>

#include "../../core/core.h"
#include "../lane/lane.h"
#include "layout.h"
#include "internal.h"

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

CanvasLayoutInternal::CanvasBounds CanvasLayoutInternal::compute_canvas_bounds(PHLMONITOR monitor) {
    static auto PGAPSINDATA = CConfigValue<Hyprlang::CUSTOMTYPE>("general:gaps_in");
    static auto PGAPSOUTDATA = CConfigValue<Hyprlang::CUSTOMTYPE>("general:gaps_out");
    auto *const PGAPSIN = (CCssGapData *)(PGAPSINDATA.ptr())->getData();
    auto *const PGAPSOUT = (CCssGapData *)(PGAPSOUTDATA.ptr())->getData();

    const auto gaps_in = PGAPSIN->m_top;
    const auto gaps_out = PGAPSOUT->m_top;
    const auto reserved = monitor->m_reservedArea;
    const auto gapOutTopLeft = Vector2D(reserved.left(), reserved.top());
    const auto gapOutBottomRight = Vector2D(reserved.right(), reserved.bottom());
    const auto size = Vector2D(monitor->m_size.x, monitor->m_size.y);
    const auto pos = Vector2D(monitor->m_position.x, monitor->m_position.y);

    return {
        .full = Box(pos, size),
        .max = Box(pos.x + gapOutTopLeft.x + gaps_out,
                   pos.y + gapOutTopLeft.y + gaps_out,
                   size.x - gapOutTopLeft.x - gapOutBottomRight.x - 2 * gaps_out,
                   size.y - gapOutTopLeft.y - gapOutBottomRight.y - 2 * gaps_out),
        .gap = static_cast<int>(gaps_in),
    };
}

PHLWORKSPACE CanvasLayout::getCanvasWorkspace() const {
    const auto algorithm = m_parent.lock();
    const auto space = algorithm ? algorithm->space() : nullptr;
    return space ? space->workspace() : nullptr;
}

Lane *CanvasLayout::getActiveLane() {
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

ListNode<Lane *> *CanvasLayout::insertLaneNode(Lane *lane, Direction direction, ListNode<Lane *> *anchor) {
    if (!lane)
        return nullptr;

    lanes.push_back(lane);
    auto node = lanes.last();
    if (!anchor || anchor == node)
        return node;

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

void CanvasLayout::finishLaneTransfer(ListNode<Lane *> *sourceLaneNode, PHLMONITOR sourceMonitor, bool ephemeralOnly, bool warpCursor) {
    if (!dropEmptyLane(sourceLaneNode, activeLane ? activeLane->data() : nullptr, sourceMonitor, ephemeralOnly))
        relayoutVisibleCanvas(sourceMonitor);

    if (const auto lane = getActiveLane()) {
        if (const auto window = lane->get_active_window())
            CanvasLayoutInternal::switch_to_window(window, warpCursor);
    }
}

Lane *CanvasLayout::getLaneForWindow(PHLWINDOW window) {
    for (auto lane = lanes.first(); lane != nullptr; lane = lane->next()) {
        if (lane->data()->has_window(window))
            return lane->data();
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

    Lane *fallbackLane = preferredLane;
    if (!fallbackLane && activeLane == laneNode) {
        if (laneNode->next())
            fallbackLane = laneNode->next()->data();
        else if (laneNode->prev())
            fallbackLane = laneNode->prev()->data();
    }

    lanes.erase(laneNode);
    delete lane;
    setActiveLane(fallbackLane);
    relayoutVisibleCanvas(fallbackMonitor);
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
            const auto delta = static_cast<double>(index) - static_cast<double>(activeIndex);
            if (mode == Mode::Row)
                laneBox = Box(max.x, max.y + delta * full.h, max.w, max.h);
            else
                laneBox = Box(max.x + delta * full.w, max.y, max.w, max.h);
        } else if (mode == Mode::Row) {
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
void CanvasLayout::newTarget(SP<Layout::ITarget> target) {
    if (!target)
        return;

    auto window = target->window();
    if (!window)
        return;

    spdlog::info("newTarget: window={} workspace={}", static_cast<const void*>(window.get()), window->workspaceID());
    onWindowCreatedTiling(window, Math::DIRECTION_DEFAULT);
    CanvasLayoutInternal::switch_to_window(window);
}

// Hyprland callback: target re-entered tiling flow and should be owned again.
void CanvasLayout::movedTarget(SP<Layout::ITarget> target, std::optional<Vector2D>)
{
    if (!target)
        return;

    auto window = target->window();
    if (!window)
        return;

    onWindowCreatedTiling(window, Math::DIRECTION_DEFAULT);
}

// Hyprland callback: remove a tiled target from canvas ownership.
void CanvasLayout::removeTarget(SP<Layout::ITarget> target)
{
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

    auto s = getLaneForWindow(window);
    if (s == nullptr) {
        if (window->m_realSize)
            *window->m_realSize = Vector2D(std::max((window->m_realSize->goal() + delta).x, 20.0), std::max((window->m_realSize->goal() + delta).y, 20.0));
        window->updateWindowDecos();
        return;
    }

    s->focus_window(window);
    s->resize_active_window(delta);
}

// Hyprland callback: relayout the whole canvas after monitor/workspace changes.
void CanvasLayout::recalculate()
{
    const auto workspace = getCanvasWorkspace();
    if (!workspace)
        return;

    const auto monitor = getVisibleCanvasMonitor();
    if (!monitor)
        return;

    relayoutCanvas(monitor, true);
}

// Placeholder for future layoutmsg support.
std::expected<void, std::string> CanvasLayout::layoutMsg(const std::string_view&)
{
    return {};
}

// Predict the size of a new tiled target using the active lane if present.
std::optional<Vector2D> CanvasLayout::predictSizeForNewTarget()
{
    auto monitor = monitorFromPointingOrCursor();
    if (!monitor)
        return {};

    auto lane = getActiveLane();
    if (!lane)
        return Vector2D(monitor->m_size.x, monitor->m_size.y);

    return lane->predict_window_size();
}

// Return the next target candidate using the active window of the active lane.
SP<Layout::ITarget> CanvasLayout::getNextCandidate(SP<Layout::ITarget> old)
{
    auto s = getActiveLane();
    if (!s)
        return {};

    const auto active = s->get_active_window();
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

// Insert a newly mapped tiled window into the active lane, creating one if needed.
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

    auto s = getActiveLane();
    if (s == nullptr) {
        s = new Lane(window);
        activeLane = insertLaneNode(s, Direction::End);
    }
    s->add_active_window(window);
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

    if (s->remove_window(window))
        return;

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
    delete doomed;

    setActiveLane(nextActiveLane);
    relayoutVisibleCanvas();
}

// Return whether this canvas currently manages a given window.
bool CanvasLayout::isWindowTiled(PHLWINDOW window)
{
    return getLaneForWindow(window) != nullptr;
}

// Recalculate only the lane that owns a given window.
void CanvasLayout::recalculateWindow(PHLWINDOW window)
{
    auto s = getLaneForWindow(window);
    if (s == nullptr)
        return;

    s->recalculate_lane_geometry();
}

void CanvasLayout::resizeActiveWindow(PHLWINDOW window, const Vector2D &delta,
                                        Layout::eRectCorner, PHLWINDOW pWindow)
{
    const auto PWINDOW = pWindow ? pWindow : window;
    if (!PWINDOW)
        return;

    auto s = getLaneForWindow(PWINDOW);
    if (s == nullptr) {
        if (PWINDOW->m_realSize)
            *PWINDOW->m_realSize = Vector2D(std::max((PWINDOW->m_realSize->goal() + delta).x, 20.0), std::max((PWINDOW->m_realSize->goal() + delta).y, 20.0));
        PWINDOW->updateWindowDecos();
        return;
    }

    s->resize_active_window(delta);
}

void CanvasLayout::alterSplitRatio(PHLWINDOW, float, bool)
{
}

void CanvasLayout::onEnable() {
    clear_lanes(lanes);
    activeLane = nullptr;
    marks.reset();
    m_focusCallback = Event::bus()->m_events.window.active.listen([this](PHLWINDOW window, Desktop::eFocusReason) {
        onWindowFocusChange(window);
    });

    const auto algorithm = m_parent.lock();
    const auto space = algorithm ? algorithm->space() : nullptr;
    const auto workspace = space ? space->workspace() : nullptr;
    if (!workspace) {
        spdlog::warn("onEnable: missing parent workspace for layout instance={}",
                     static_cast<const void*>(this));
        return;
    }

    spdlog::info("onEnable: rebuilding layout instance={} workspace={} from mapped windows",
                 static_cast<const void*>(this), workspace->m_id);
    for (auto& window : g_pCompositor->m_windows) {
        spdlog::debug("onEnable: candidate instance={} window={} workspace={} mapped={} hidden={} floating={}",
                      static_cast<const void*>(this),
                      static_cast<const void*>(window.get()), window->workspaceID(), window->m_isMapped,
                      window->isHidden(), window->m_isFloating);
        if (window->workspaceID() != workspace->m_id || window->m_isFloating || !window->m_isMapped)
            continue;

        spdlog::info("onEnable: registering instance={} window={} workspace={} hidden={}",
                     static_cast<const void*>(this), static_cast<const void*>(window.get()),
                     window->workspaceID(), window->isHidden());
        onWindowCreatedTiling(window);
    }

    const auto monitor = CanvasLayoutInternal::visible_monitor_for_workspace(workspace);
    if (!monitor) {
        spdlog::debug("onEnable: no visible monitor for instance={} workspace={}",
                      static_cast<const void*>(this), workspace->m_id);
        return;
    }

    relayoutCanvas(monitor, !workspace->m_isSpecialWorkspace);
}

void CanvasLayout::onDisable() {
    m_focusCallback = nullptr;
    clear_lanes(lanes);
    activeLane = nullptr;
    marks.reset();
}

Vector2D CanvasLayout::predictSizeForNewWindowTiled() {
    auto monitor = monitorFromPointingOrCursor();
    if (!monitor)
        return {};

    auto s = getActiveLane();
    if (s == nullptr)
        return monitor->m_size;

    return s->predict_window_size();
}

void CanvasLayout::replaceWindowDataWith(PHLWINDOW, PHLWINDOW)
{
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
        CanvasLayoutInternal::switch_to_window(window);
}

void CanvasLayout::marks_reset() {
    marks.reset();
}
