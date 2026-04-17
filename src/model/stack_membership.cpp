/**
 * @file stack_membership.cpp
 * @brief Stack membership, focus movement, and active-window transitions.
 */
#include "stack_internal.h"

#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/managers/KeybindManager.hpp>

#include "../core/layout_profile.h"

#include "stack_logic.h"

namespace ScrollerModel {

ListNode<Window *> *Stack::findWindowNode(PHLWINDOW window) const {
    if (!window)
        return nullptr;

    for (auto win = windows.first(); win != nullptr; win = win->next()) {
        if (win->data()->ptr().lock() == window)
            return win;
    }

    return nullptr;
}

bool Stack::has_window(PHLWINDOW window) const {
    return findWindowNode(window) != nullptr;
}

bool Stack::swap_windows(PHLWINDOW a, PHLWINDOW b) {
    if (a == b)
        return false;

    auto *na = findWindowNode(a);
    auto *nb = findWindowNode(b);
    if (!na || !nb)
        return false;

    windows.swap(na, nb);
    if (active == na)
        active = nb;
    else if (active == nb)
        active = na;
    return true;
}

void Stack::remove_window(PHLWINDOW window) {
    reorder = Reorder::Auto;
    auto *win = findWindowNode(window);
    if (!win)
        return;

    if (active && window == active->data()->ptr().lock())
        active = StackLogic::next_active_after_removal(active, windows.last());

    auto *removed = win->data();
    windows.erase(win);
    delete removed;
    if (windows.size() != 1 || !active)
        return;

    active->data()->update_height(WindowHeight::One, StackInternal::stack_local_span(geom, mode));
}

void Stack::focus_window(PHLWINDOW window) {
    if (auto *win = findWindowNode(window))
        active = win;
}

PHLWINDOW Stack::get_active_window() {
    return active ? active->data()->ptr().lock() : nullptr;
}

bool Stack::active_at_edge(Direction direction) const {
    if (!active)
        return false;

    switch (direction) {
    case Direction::Left:
        return mode == Mode::Column && active == windows.first();
    case Direction::Right:
        return mode == Mode::Column && active == windows.last();
    case Direction::Up:
        return mode == Mode::Row && active == windows.first();
    case Direction::Down:
        return mode == Mode::Row && active == windows.last();
    default:
        return false;
    }
}

void Stack::move_active(Direction direction) {
    if (!active)
        return;

    const auto backward = ScrollerCore::stack_item_backward_direction(mode);
    const auto forward = ScrollerCore::stack_item_forward_direction(mode);
    if (direction == backward && active != windows.first()) {
        reorder = Reorder::Auto;
        auto previous = active->prev();
        windows.swap(active, previous);
        return;
    }

    if (direction == forward && active != windows.last()) {
        reorder = Reorder::Auto;
        auto next = active->next();
        windows.swap(active, next);
    }
}

FocusMoveResult Stack::move_focus(Direction direction, bool focus_wrap) {
    if (!active)
        return FocusMoveResult::NoOp;

    const auto backward = ScrollerCore::stack_item_backward_direction(mode);
    const auto forward = ScrollerCore::stack_item_forward_direction(mode);
    if (direction == backward && active != windows.first()) {
        reorder = Reorder::Auto;
        active = active->prev();
        return FocusMoveResult::Moved;
    }

    if (direction == forward && active != windows.last()) {
        reorder = Reorder::Auto;
        active = active->next();
        return FocusMoveResult::Moved;
    }

    auto monitorDirection = [&]() -> Math::eDirection {
        switch (direction) {
        case Direction::Left:
            return Math::fromChar('l');
        case Direction::Right:
            return Math::fromChar('r');
        case Direction::Up:
            return Math::fromChar('u');
        case Direction::Down:
        default:
            return Math::fromChar('d');
        }
    }();

    if (g_pCompositor->getMonitorInDirection(monitorDirection) != nullptr)
        return FocusMoveResult::CrossMonitor;

    auto previous = active;
    if (focus_wrap)
        active = direction == backward ? windows.last() : windows.first();
    return active != previous ? FocusMoveResult::Moved : FocusMoveResult::NoOp;
}

void Stack::admit_window(std::unique_ptr<Window> window) {
    reorder = Reorder::Auto;
    if (!window)
        return;

    if (active) {
        const auto activeWindow = active->data();
        window->set_geom_h(activeWindow->get_geom_h());
        window->set_geom_y(activeWindow->get_geom_y() + activeWindow->get_geom_h());
    } else {
        window->set_geom_h(StackInternal::stack_local_span(geom, mode));
        window->set_geom_y(StackInternal::stack_local_origin(geom, mode));
    }

    active = windows.emplace_after(active, window.release());
}

void Stack::restore_window(std::unique_ptr<Window> window, bool insertBeforeActive) {
    reorder = Reorder::Auto;
    if (!window)
        return;

    if (!active) {
        window->set_geom_h(StackInternal::stack_local_span(geom, mode));
        window->set_geom_y(StackInternal::stack_local_origin(geom, mode));
        active = windows.emplace_after(active, window.release());
        return;
    }

    active = insertBeforeActive
        ? windows.emplace_before(active, window.release())
        : windows.emplace_after(active, window.release());
}

std::unique_ptr<Window> Stack::expel_active(double /*gap*/) {
    reorder = Reorder::Auto;
    if (!active)
        return {};

    std::unique_ptr<Window> window(active->data());
    auto *nextActive = StackLogic::next_active_after_removal(active, windows.last());
    windows.erase(active);
    active = nextActive;
    return window;
}

void Stack::align_window(Direction direction, double /*gap*/) {
    if (!active)
        return;

    const auto backward = ScrollerCore::stack_item_backward_direction(mode);
    const auto forward = ScrollerCore::stack_item_forward_direction(mode);
    const auto target = StackLogic::aligned_local_position(
        direction,
        backward,
        forward,
        StackInternal::stack_local_origin(geom, mode),
        StackInternal::stack_local_span(geom, mode),
        active->data()->get_geom_h());
    if (!target)
        return;

    reorder = Reorder::Lazy;
    active->data()->set_geom_y(*target);
}

} // namespace ScrollerModel
