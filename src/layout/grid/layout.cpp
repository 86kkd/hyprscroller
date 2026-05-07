#include "layout/grid/layout.h"

#include <hyprland/src/Compositor.hpp>
#include <hyprutils/math/Box.hpp>
#include <spdlog/spdlog.h>

#include "core/core.h"
#include "core/window_key.h"
#include "layout/canvas/internal.h"

namespace ScrollerGrid {
namespace {

std::optional<Direction> direction_from_hypr(Math::eDirection direction) {
    switch (direction) {
    case Math::DIRECTION_LEFT:
        return Direction::Left;
    case Math::DIRECTION_RIGHT:
        return Direction::Right;
    case Math::DIRECTION_UP:
        return Direction::Up;
    case Math::DIRECTION_DOWN:
        return Direction::Down;
    default:
        return std::nullopt;
    }
}

void sync_window_target_geometry(PHLWINDOW window) {
    if (!window)
        return;

    const auto target = window->layoutTarget();
    if (!target)
        return;

    target->setPositionGlobal(Hyprutils::Math::CBox(window->m_position, window->m_size));
}

} // namespace

PHLMONITOR GridLayout::resolve_monitor() const {
    if (const auto window = reference_window())
        return g_pCompositor->getMonitorFromID(window->monitorID());

    return ScrollerCore::monitorFromPointingOrCursor();
}

PHLWINDOW GridLayout::reference_window() const {
    if (const auto active = active_window())
        return active;

    for (const auto& [_, window] : windowsByKey) {
        if (window)
            return window;
    }

    return nullptr;
}

PHLWINDOW GridLayout::active_window() const {
    const auto* active = model.active_item();
    if (!active)
        return nullptr;

    const auto it = windowsByKey.find(active->key);
    return it == windowsByKey.end() ? nullptr : it->second;
}

GridProfile GridLayout::current_profile(PHLMONITOR monitor) const {
    if (!monitor)
        return {};

    return profile_for_workarea_extent(CanvasLayoutInternal::compute_canvas_bounds(monitor).max);
}

void GridLayout::relayout(PHLMONITOR monitor) {
    if (!monitor)
        return;

    const auto bounds = CanvasLayoutInternal::compute_canvas_bounds(monitor);
    const auto profile = profile_for_workarea_extent(bounds.max);
    for (const auto& item : model.render(viewport, profile, bounds.full, bounds.max)) {
        const auto it = windowsByKey.find(item.key);
        if (it == windowsByKey.end() || !it->second)
            continue;

        it->second->m_position = {item.committedBox.x, item.committedBox.y};
        it->second->m_size = {item.committedBox.w, item.committedBox.h};
        sync_window_target_geometry(it->second);
    }
}

void GridLayout::newTarget(SP<Layout::ITarget> target) {
    const auto window = ScrollerCore::windowFromTarget(target);
    if (!window)
        return;

    const auto monitor = g_pCompositor->getMonitorFromID(window->monitorID());
    const auto profile = current_profile(monitor);
    const auto key = ScrollerCore::window_key(window);
    windowsByKey[key] = window;

    if (!model.add_window(key, profile))
        (void)model.focus_window(key);
    model.ensure_active_visible(profile, viewport);

    relayout(monitor);
}

void GridLayout::movedTarget(SP<Layout::ITarget> target, std::optional<Vector2D>) {
    newTarget(target);
}

void GridLayout::removeTarget(SP<Layout::ITarget> target) {
    const auto window = ScrollerCore::windowFromTarget(target);
    if (!window)
        return;

    const auto monitor = g_pCompositor->getMonitorFromID(window->monitorID());
    const auto key = ScrollerCore::window_key(window);
    windowsByKey.erase(key);
    model.remove_window(key);
    relayout(monitor ? monitor : resolve_monitor());
}

void GridLayout::resizeTarget(const Vector2D&, SP<Layout::ITarget>, Layout::eRectCorner) {
    relayout(resolve_monitor());
}

void GridLayout::recalculate() {
    relayout(resolve_monitor());
}

std::expected<void, std::string> GridLayout::layoutMsg(const std::string_view& message) {
    spdlog::warn("grid layoutMsg: unsupported message='{}'", message);
    return std::unexpected("grid layout messages are not supported yet");
}

std::optional<Vector2D> GridLayout::predictSizeForNewTarget() {
    const auto monitor = resolve_monitor();
    if (!monitor)
        return {};

    const auto profile = current_profile(monitor);
    return Vector2D(profile.unitWidth, profile.unitHeight);
}

SP<Layout::ITarget> GridLayout::getNextCandidate(SP<Layout::ITarget>) {
    const auto active = active_window();
    return active ? active->layoutTarget() : nullptr;
}

void GridLayout::swapTargets(SP<Layout::ITarget> a, SP<Layout::ITarget> b) {
    const auto wa = ScrollerCore::windowFromTarget(a);
    const auto wb = ScrollerCore::windowFromTarget(b);
    if (!wa || !wb)
        return;

    const auto keyA = ScrollerCore::window_key(wa);
    const auto keyB = ScrollerCore::window_key(wb);
    if (!model.swap_windows(keyA, keyB))
        return;

    relayout(resolve_monitor());
}

void GridLayout::moveTargetInDirection(SP<Layout::ITarget> target, Math::eDirection direction, bool) {
    const auto parsed = direction_from_hypr(direction);
    if (!parsed)
        return;

    if (const auto window = ScrollerCore::windowFromTarget(target))
        (void)model.focus_window(ScrollerCore::window_key(window));

    const auto monitor = resolve_monitor();
    const auto profile = current_profile(monitor);
    (void)model.move_focus(*parsed, profile, viewport, false);
    relayout(monitor);
}

void GridLayout::move_focus(int workspace, Direction direction) {
    (void)workspace;

    const auto monitor = resolve_monitor();
    const auto profile = current_profile(monitor);
    if (model.move_focus(direction, profile, viewport, false) != GridMoveResult::Moved)
        return;

    relayout(monitor);
    (void)CanvasLayoutInternal::switch_to_window(active_window(), true);
}

void GridLayout::move_window(int workspace, Direction direction) {
    (void)workspace;

    const auto monitor = resolve_monitor();
    const auto profile = current_profile(monitor);
    if (model.move_active_window(direction, profile, viewport) != GridMoveResult::Moved)
        return;

    relayout(monitor);
    (void)CanvasLayoutInternal::switch_to_window(active_window(), true);
}

void GridLayout::focus_window(PHLWINDOW window) {
    if (!window)
        return;

    const auto key = ScrollerCore::window_key(window);
    if (!model.focus_window(key))
        return;

    const auto monitor = g_pCompositor->getMonitorFromID(window->monitorID());
    model.ensure_active_visible(current_profile(monitor), viewport);
    relayout(monitor);
}

void GridLayout::recalculateMonitor(const int& monitorId) {
    relayout(g_pCompositor->getMonitorFromID(monitorId));
}

void GridLayout::prepareForOverviewSnapshot() {
    relayout(resolve_monitor());
}

CanvasOverviewSnapshot GridLayout::buildOverviewSnapshot() const {
    CanvasOverviewSnapshot snapshot;

    const auto window = reference_window();
    if (!window)
        return snapshot;

    snapshot.workspaceId = window->workspaceID();
    const auto monitor = g_pCompositor->getMonitorFromID(window->monitorID());
    snapshot.monitorId = monitor ? monitor->m_id : window->monitorID();
    if (!monitor)
        return snapshot;

    const auto bounds = CanvasLayoutInternal::compute_canvas_bounds(monitor);
    const auto profile = profile_for_workarea_extent(bounds.max);
    for (const auto& item : model.render(viewport, profile, bounds.full, bounds.max)) {
        const auto it = windowsByKey.find(item.key);
        if (it == windowsByKey.end() || !it->second)
            continue;

        snapshot.windows.push_back({
            .window = it->second,
            .box = item.logicalBox,
        });
    }

    return snapshot;
}

} // namespace ScrollerGrid
