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

static Marks marks;

namespace {
ListNode<Lane*>* find_lane_node(List<Lane*>& lanes, Lane* target) {
    for (auto lane = lanes.first(); lane != nullptr; lane = lane->next()) {
        if (lane->data() == target)
            return lane;
    }
    return nullptr;
}

void clear_lanes(List<Lane*>& lanes) {
    for (auto lane = lanes.first(); lane != nullptr; lane = lane->next())
        delete lane->data();
    lanes.clear();
}

bool has_ephemeral_lane(const List<Lane*>& lanes) {
    for (auto lane = lanes.first(); lane != nullptr; lane = lane->next()) {
        if (lane->data() && lane->data()->is_ephemeral())
            return true;
    }
    return false;
}

size_t lane_index(const List<Lane*>& lanes, const ListNode<Lane*>* target) {
    size_t index = 0;
    for (auto lane = lanes.first(); lane != nullptr; lane = lane->next(), ++index) {
        if (lane == target)
            return index;
    }
    return 0;
}
} // namespace

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

void CanvasLayout::setActiveLane(Lane *lane) {
    activeLane = lane ? find_lane_node(lanes, lane) : nullptr;
}

Lane *CanvasLayout::getLaneForWindow(PHLWINDOW window) {
    for (auto lane = lanes.first(); lane != nullptr; lane = lane->next()) {
        if (lane->data()->has_window(window))
            return lane->data();
    }
    return nullptr;
}

void CanvasLayout::relayoutCanvas(PHLMONITOR monitor, bool honor_fullscreen) {
    const auto workspace = getCanvasWorkspace();
    if (!workspace || !monitor || lanes.empty())
        return;

    if (lanes.size() == 1) {
        CanvasLayoutInternal::recalculate_workspace_lane(lanes.first()->data(), monitor, workspace, honor_fullscreen);
        return;
    }

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
    const auto full = Box(pos, size);
    const auto max = Box(pos.x + gapOutTopLeft.x + gaps_out,
                         pos.y + gapOutTopLeft.y + gaps_out,
                         size.x - gapOutTopLeft.x - gapOutBottomRight.x - 2 * gaps_out,
                         size.y - gapOutTopLeft.y - gapOutBottomRight.y - 2 * gaps_out);

    const auto mode = getActiveLane() ? getActiveLane()->get_mode() : Mode::Row;
    const auto paged = has_ephemeral_lane(lanes);
    const auto count = static_cast<double>(lanes.size());
    const auto activeIndex = lane_index(lanes, activeLane ? activeLane : lanes.first());
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

        lane->data()->set_canvas_geometry(full, laneBox, gaps_in);
        lane->data()->recalculate_lane_geometry();
    }
}

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

void CanvasLayout::movedTarget(SP<Layout::ITarget> target, std::optional<Vector2D>)
{
    if (!target)
        return;

    auto window = target->window();
    if (!window)
        return;

    onWindowCreatedTiling(window, Math::DIRECTION_DEFAULT);
}

void CanvasLayout::removeTarget(SP<Layout::ITarget> target)
{
    if (!target)
        return;

    onWindowRemovedTiling(target->window());
}

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

void CanvasLayout::recalculate()
{
    const auto workspace = getCanvasWorkspace();
    if (!workspace)
        return;

    const auto monitor = CanvasLayoutInternal::visible_monitor_for_workspace(workspace);
    if (!monitor)
        return;

    relayoutCanvas(monitor, true);
}

std::expected<void, std::string> CanvasLayout::layoutMsg(const std::string_view&)
{
    return {};
}

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

void CanvasLayout::moveTargetInDirection(SP<Layout::ITarget> t, Math::eDirection direction, bool)
{
    auto window = windowFromTarget(t);
    auto s = getLaneForWindow(window);
    if (!s || !window)
        return;

    s->focus_window(window);
    switch (direction) {
        case Math::DIRECTION_LEFT:
            s->move_active_stack(Direction::Left);
            break;
        case Math::DIRECTION_RIGHT:
            s->move_active_stack(Direction::Right);
            break;
        case Math::DIRECTION_UP:
            s->move_active_stack(Direction::Up);
            break;
        case Math::DIRECTION_DOWN:
            s->move_active_stack(Direction::Down);
            break;
        default:
            return;
    }
}

void CanvasLayout::onWindowCreatedTiling(PHLWINDOW window, Math::eDirection)
{
    auto s = getActiveLane();
    if (s == nullptr) {
        s = new Lane(window);
        lanes.push_back(s);
        activeLane = lanes.last();
    }
    s->add_active_window(window);
}

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

    auto lane = find_lane_node(lanes, s);
    if (!lane) {
        spdlog::warn("onWindowRemovedTiling: empty lane missing from list lane={} workspace={}",
                     static_cast<const void*>(s), workspace);
        return;
    }

    auto doomed = lane->data();
    spdlog::info("onWindowRemovedTiling: deleting empty lane={} workspace={}",
                 static_cast<const void*>(doomed), workspace);
    if (activeLane == lane)
        activeLane = lane->next() ? lane->next() : lane->prev();
    lanes.erase(lane);
    delete doomed;
}

bool CanvasLayout::isWindowTiled(PHLWINDOW window)
{
    return getLaneForWindow(window) != nullptr;
}

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
