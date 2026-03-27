/**
 * @file actions.cpp
 * @brief Command-facing lane operations and focus movement.
 *
 * These methods mutate the active lane in response to user commands: inserting
 * and removing windows, moving focus, reordering stacks, and splitting/merging
 * windows between neighboring stacks.
 */
#include "lane.h"

#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/helpers/Monitor.hpp>

#include "../../core/layout_profile.h"

// Insert a new window into the active lane, respecting the current lane mode.
void Lane::add_active_window(PHLWINDOW window) {
    if (ScrollerCore::mode_adds_windows_into_active_stack(mode) && active != nullptr) {
        const auto windowCountBefore = active->data()->size();
        active->data()->add_active_window(window, 0.5 * max.h);
        rememberWindowStack(window, active->data());
        if (windowCountBefore == 1) {
            active->data()->fit_size(FitSize::All, calculate_gap_x(active), gap);
        } else {
            active->data()->recalculate_stack_geometry(calculate_gap_x(active), gap);
        }
        debugVerifyStackCache();
        return;
    }

    const bool singleWindowWorkspace = stacks.size() == 1 && stacks.first()->data()->size() == 1;
    if (singleWindowWorkspace)
        stacks.first()->data()->update_width(StackWidth::OneHalf, max.w, max.h);

    active = stacks.emplace_after(active, new Stack(window, max.w, max.h));
    rememberWindowStack(window, active->data());
    if (singleWindowWorkspace)
        active->data()->update_width(StackWidth::OneHalf, max.w, max.h);
    reorder = Reorder::Auto;
    recalculate_lane_geometry();
    debugVerifyStackCache();
}

// Remove a window from this lane and keep stack/lane state coherent.
bool Lane::remove_window(PHLWINDOW window) {
    reorder = Reorder::Auto;
    auto *col = getStackForWindow(window);
    auto *c = getStackNode(col);
    if (!col || !c)
        return true;

    forgetWindowStack(window);
    col->remove_window(window);
    if (col->size() == 0) {
        if (c == active)
            active = active != stacks.last() ? active->next() : active->prev();

        forgetStackWindows(col);
        auto *doomed = col;
        stacks.erase(c);
        delete doomed;
        if (stacks.empty()) {
            debugVerifyStackCache();
            return false;
        }

        recalculate_lane_geometry();
        debugVerifyStackCache();
        return true;
    }

    if (ScrollerCore::mode_uses_window_expansion(mode)) {
        if (col->size() <= 2)
            col->fit_size(FitSize::All, calculate_gap_x(c), gap);
        else
            col->recalculate_stack_geometry(calculate_gap_x(c), gap);
    } else {
        col->recalculate_stack_geometry(calculate_gap_x(c), gap);
    }
    debugVerifyStackCache();
    return true;
}

// Swap two windows when they both belong to the same stack in this lane.
bool Lane::swapWindows(PHLWINDOW a, PHLWINDOW b) {
    auto *stackA = getStackForWindow(a);
    auto *stackB = getStackForWindow(b);
    if (!stackA || !stackB || stackA != stackB)
        return false;

    return stackA->swap_windows(a, b);
}

// Focus the stack and window that owns the given compositor window.
void Lane::focus_window(PHLWINDOW window) {
    auto *stack = getStackForWindow(window);
    auto *stackNode = getStackNode(stack);
    if (!stack || !stackNode)
        return;

    stack->focus_window(window);
    active = stackNode;
    recalculate_lane_geometry();
}

// Report whether the active stack/window is already at the requested edge.
bool Lane::active_item_at_edge(Direction direction) const {
    if (!active)
        return false;

    switch (direction) {
    case Direction::Left:
        return ScrollerCore::local_item_backward_direction(mode) == Direction::Left && active == stacks.first();
    case Direction::Right:
        return ScrollerCore::local_item_forward_direction(mode) == Direction::Right && active == stacks.last();
    case Direction::Up:
        return ScrollerCore::local_item_backward_direction(mode) == Direction::Up && active->data()->active_at_edge(Direction::Up);
    case Direction::Down:
        return ScrollerCore::local_item_forward_direction(mode) == Direction::Down && active->data()->active_at_edge(Direction::Down);
    default:
        return false;
    }
}

// Execute directional focus movement inside this lane.
FocusMoveResult Lane::move_focus(Direction dir, bool focus_wrap) {
    if (!active)
        return FocusMoveResult::NoOp;

    reorder = Reorder::Auto;
    FocusMoveResult result = FocusMoveResult::NoOp;
    switch (dir) {
    case Direction::Left:
        result = move_focus_left(focus_wrap);
        break;
    case Direction::Right:
        result = move_focus_right(focus_wrap);
        break;
    case Direction::Up:
        result = active->data()->move_focus_up(focus_wrap);
        break;
    case Direction::Down:
        result = active->data()->move_focus_down(focus_wrap);
        break;
    case Direction::Begin:
        if (active != stacks.first()) {
            move_focus_begin();
            result = FocusMoveResult::Moved;
        }
        break;
    case Direction::End:
        if (active != stacks.last()) {
            move_focus_end();
            result = FocusMoveResult::Moved;
        }
        break;
    default:
        return FocusMoveResult::NoOp;
    }
    if (result != FocusMoveResult::Moved)
        return result;

    recalculate_lane_geometry();
    return result;
}

// Move focus to the previous stack, wrapping or crossing monitor when needed.
FocusMoveResult Lane::move_focus_left(bool focus_wrap) {
    if (active == stacks.first()) {
        PHLMONITOR monitor = g_pCompositor->getMonitorInDirection(Math::fromChar('l'));
        if (monitor == nullptr) {
            auto previous = active;
            if (focus_wrap)
                active = stacks.last();
            return active != previous ? FocusMoveResult::Moved : FocusMoveResult::NoOp;
        }
        return FocusMoveResult::CrossMonitor;
    }
    active = active->prev();
    return FocusMoveResult::Moved;
}

// Move focus to the next stack, wrapping or crossing monitor when needed.
FocusMoveResult Lane::move_focus_right(bool focus_wrap) {
    if (active == stacks.last()) {
        PHLMONITOR monitor = g_pCompositor->getMonitorInDirection(Math::fromChar('r'));
        if (monitor == nullptr) {
            auto previous = active;
            if (focus_wrap)
                active = stacks.first();
            return active != previous ? FocusMoveResult::Moved : FocusMoveResult::NoOp;
        }
        return FocusMoveResult::CrossMonitor;
    }
    active = active->next();
    return FocusMoveResult::Moved;
}

// Jump focus to the first stack in the lane.
void Lane::move_focus_begin() {
    active = stacks.first();
}

// Jump focus to the last stack in the lane.
void Lane::move_focus_end() {
    active = stacks.last();
}

// Cycle the active stack width or active window height, depending on mode.
void Lane::resize_active_stack(int step) {
    if (!active)
        return;

    if (active->data()->maximized())
        return;

    if (ScrollerCore::mode_uses_window_expansion(mode)) {
        active->data()->cycle_size_active_window(step, calculate_gap_x(active), gap);
        return;
    }

    StackWidth width = active->data()->get_width();
    if (width == StackWidth::Free) {
        width = StackWidth::OneHalf;
    } else {
        int number = static_cast<int>(StackWidth::Number);
        width = static_cast<StackWidth>((number + static_cast<int>(width) + step) % number);
    }
    active->data()->update_width(width, max.w, max.h);
    reorder = Reorder::Auto;
    recalculate_lane_geometry();
}

// Resize the active window inside the current stack when resizing is allowed.
void Lane::resize_active_window(const Vector2D &delta) {
    if (!active)
        return;

    if (active->data()->maximized() ||
        active->data()->fullscreen() ||
        active->data()->expanded())
        return;

    active->data()->resize_active_window(max.w, calculate_gap_x(active), gap, delta);
    recalculate_lane_geometry();
}

// Change the lane traversal mode used by focus and insertion logic.
void Lane::set_mode(Mode m) {
    mode = m;
}

// Align the active stack or active window against the current lane viewport.
void Lane::align_stack(Direction dir) {
    if (!active)
        return;

    if (active->data()->maximized() ||
        active->data()->fullscreen() ||
        active->data()->expanded())
        return;

    switch (dir) {
    case Direction::Left:
        active->data()->set_geom_pos(max.x, max.y);
        break;
    case Direction::Right:
        active->data()->set_geom_pos(max.x + max.w - active->data()->get_geom_w(), max.y);
        break;
    case Direction::Center:
        if (ScrollerCore::mode_uses_window_expansion(mode)) {
            active->data()->align_window(Direction::Center, gap);
            active->data()->recalculate_stack_geometry(calculate_gap_x(active), gap);
        } else {
            center_active_stack();
        }
        break;
    case Direction::Up:
    case Direction::Down:
        active->data()->align_window(dir, gap);
        active->data()->recalculate_stack_geometry(calculate_gap_x(active), gap);
        break;
    default:
        return;
    }
    reorder = Reorder::Lazy;
    recalculate_lane_geometry();
}

// Reorder stacks in row mode or windows in column mode.
void Lane::move_active_stack(Direction dir) {
    if (!active)
        return;

    switch (dir) {
    case Direction::Right:
        if (active != stacks.last()) {
            auto next = active->next();
            stacks.swap(active, next);
        }
        break;
    case Direction::Left:
        if (active != stacks.first()) {
            auto prev = active->prev();
            stacks.swap(active, prev);
        }
        break;
    case Direction::Up:
        active->data()->move_active_up();
        break;
    case Direction::Down:
        active->data()->move_active_down();
        break;
    case Direction::Begin:
        if (active != stacks.first())
            stacks.move_before(stacks.first(), active);
        break;
    case Direction::End:
        if (active != stacks.last())
            stacks.move_after(stacks.last(), active);
        break;
    case Direction::Center:
        return;
    }

    reorder = Reorder::Auto;
    recalculate_lane_geometry();
}

// Move the active window into the previous stack.
void Lane::admit_window_left() {
    if (!active)
        return;

    if (active->data()->maximized() ||
        active->data()->fullscreen() ||
        active->data()->expanded() ||
        active == stacks.first())
        return;

    auto w = active->data()->expel_active(gap);
    const auto movedWindow = w ? w->ptr().lock() : nullptr;
    forgetWindowStack(movedWindow);
    auto prev = active->prev();
    if (active->data()->size() == 0) {
        auto *doomed = active->data();
        auto *emptyNode = active;
        stacks.erase(emptyNode);
        forgetStackWindows(doomed);
        delete doomed;
    }
    active = prev;
    active->data()->admit_window(std::move(w));
    rememberWindowStack(movedWindow, active->data());

    reorder = Reorder::Auto;
    recalculate_lane_geometry();
    debugVerifyStackCache();
}

// Split the active window into a new stack to the right.
void Lane::expel_window_right() {
    if (active->data()->maximized() ||
        active->data()->fullscreen() ||
        active->data()->expanded() ||
        active->data()->size() == 1)
        return;

    auto w = active->data()->expel_active(gap);
    const auto movedWindow = w ? w->ptr().lock() : nullptr;
    forgetWindowStack(movedWindow);
    StackWidth width = active->data()->get_width();
    double maxw = width == StackWidth::Free ? active->data()->get_geom_w() : max.w;
    active = stacks.emplace_after(active, new Stack(std::move(w), width, maxw, max.h));
    rememberWindowStack(movedWindow, active->data());
    active->data()->set_geom_pos(active->prev()->data()->get_geom_x() + active->prev()->data()->get_geom_w(), max.y);

    reorder = Reorder::Auto;
    recalculate_lane_geometry();
    debugVerifyStackCache();
}

// Fit stack/window sizes to the requested visible range.
void Lane::fit_size(FitSize fitsize) {
    if (ScrollerCore::mode_uses_window_expansion(mode)) {
        active->data()->fit_size(fitsize, calculate_gap_x(active), gap);
        return;
    }
    ListNode<Stack *> *from = nullptr;
    ListNode<Stack *> *to = nullptr;
    switch (fitsize) {
    case FitSize::Active:
        from = to = active;
        break;
    case FitSize::Visible:
        for (auto c = stacks.first(); c != nullptr; c = c->next()) {
            Stack *col = c->data();
            auto c0 = col->get_geom_x();
            auto c1 = col->get_geom_x() + col->get_geom_w();
            if ((c0 < max.x + max.w && c0 >= max.x) ||
                (c1 > max.x && c1 <= max.x + max.w) ||
                (c0 < max.x && c1 >= max.x + max.w)) {
                from = c;
                break;
            }
        }
        for (auto c = stacks.last(); c != nullptr; c = c->prev()) {
            Stack *col = c->data();
            auto c0 = col->get_geom_x();
            auto c1 = col->get_geom_x() + col->get_geom_w();
            if ((c0 < max.x + max.w && c0 >= max.x) ||
                (c1 > max.x && c1 <= max.x + max.w) ||
                (c0 < max.x && c1 >= max.x + max.w)) {
                to = c;
                break;
            }
        }
        break;
    case FitSize::All:
        from = stacks.first();
        to = stacks.last();
        break;
    case FitSize::ToEnd:
        from = active;
        to = stacks.last();
        break;
    case FitSize::ToBeg:
        from = stacks.first();
        to = active;
        break;
    default:
        return;
    }

    if (from != nullptr && to != nullptr) {
        double total = 0.0;
        for (auto c = from; c != to->next(); c = c->next())
            total += c->data()->get_geom_w();
        if (total <= 0.0)
            return;

        for (auto c = from; c != to->next(); c = c->next()) {
            Stack *col = c->data();
            col->set_width_free();
            col->set_geom_w(col->get_geom_w() / total * max.w);
        }
        from->data()->set_geom_pos(max.x, max.y);
        adjust_stacks(from);
    }
}
