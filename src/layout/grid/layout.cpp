#include "layout/grid/layout.h"

#include <hyprland/src/Compositor.hpp>
#include <hyprutils/math/Box.hpp>
#include <spdlog/spdlog.h>

#include "core/core.h"
#include "core/monitor_geometry_runtime.h"
#include "core/window_key.h"

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
    if (const auto active = active_window())
        return g_pCompositor->getMonitorFromID(active->monitorID());

    return ScrollerCore::monitorFromPointingOrCursor();
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

    return profile_for_workarea_extent(ScrollerCore::logical_workarea_box(monitor, 0.0));
}

void GridLayout::relayout(PHLMONITOR monitor) {
    if (!monitor)
        return;

    const auto full = ScrollerCore::logical_monitor_box(monitor);
    const auto workarea = ScrollerCore::logical_workarea_box(monitor, 0.0);
    const auto profile = profile_for_workarea_extent(workarea);
    for (const auto& item : model.render(viewport, profile, full, workarea)) {
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

} // namespace ScrollerGrid
