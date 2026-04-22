/**
 * @file stack_membership.cpp
 * @brief Stack membership, focus movement, and active-window transitions.
 *
 * Geometry code assumes the linked-list order of `windows` is authoritative.
 * The helpers in this file therefore do two jobs together: mutate that list and
 * keep the `active` cursor pointing at the logical window the user expects.
 */
#include "model/stack/internal.h"

#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/managers/KeybindManager.hpp>

#include "core/layout_profile.h"

#include "model/stack/logic.h"

namespace ScrollerModel {

ListNode<Window *> *Stack::findWindowNode(PHLWINDOW window) const {
    if (!window)
        return nullptr;

    // The list is usually short, so a linear scan keeps membership logic simple
    // and avoids maintaining extra pointer-to-node indexes.
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

    // Swapping list nodes would otherwise leave `active` pointing at the old
    // node, so we repair the cursor to continue tracking the same logical window.
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

    // When the active node is removed, pick the successor that should inherit
    // focus before erasing the node so we do not lose neighborhood context.
    if (active && window == active->data()->ptr().lock())
        active = StackLogic::next_active_after_removal(active, windows.last());

    auto *removed = win->data();
    windows.erase(win);
    delete removed;
    if (windows.size() != 1 || !active)
        return;

    // A single surviving window should reset to the canonical full-height state
    // rather than preserving whatever fragment size it previously had.
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

    // Reaching the stack edge does not always mean "stop". We first ask whether
    // Hyprland has another monitor in that direction; if so, the caller may
    // want cross-monitor focus instead of stack-local wrapping.
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

    // Only once there is no monitor hand-off available do we optionally wrap to
    // the opposite end of the stack.
    auto previous = active;
    if (focus_wrap)
        active = direction == backward ? windows.last() : windows.first();
    return active != previous ? FocusMoveResult::Moved : FocusMoveResult::NoOp;
}

void Stack::admit_window(std::unique_ptr<Window> window) {
    reorder = Reorder::Auto;
    if (!window)
        return;

    // New windows are inserted adjacent to the active window and inherit its
    // size so the subsequent relayout can start from a stable local geometry.
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

    // Restoring a detached window differs from admitting a brand-new one: we
    // preserve its remembered size/position unless the stack is currently empty.
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

    // Ownership is transferred back to the caller. We compute the replacement
    // active node before erasing so later code still has a valid focus cursor.
    std::unique_ptr<Window> window(active->data());
    auto *nextActive = StackLogic::next_active_after_removal(active, windows.last());
    windows.erase(active);
    active = nextActive;
    return window;
}

void Stack::align_window(Direction direction, double /*gap*/) {
    if (!active)
        return;

    // Alignment snaps the active window to one logical edge/center position
    // without immediately reordering the whole stack around it.
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

    // Lazy reorder preserves this exact local position until a later relayout
    // chooses to normalize neighbors around the active window again.
    reorder = Reorder::Lazy;
    active->data()->set_geom_y(*target);
}

} // namespace ScrollerModel
