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

#include "../../core/fit_size.h"
#include "../../core/interval.h"
#include "../../core/layout_profile.h"

using ScrollerModel::Reorder;
using ScrollerModel::Stack;
using ScrollerModel::StackWidth;

// Insert a new window into the active lane as a peer stack.
void Lane::add_active_window(PHLWINDOW window) {
    const bool singleWindowWorkspace = stacks.size() == 1 && stacks.first()->data()->size() == 1;
    if (singleWindowWorkspace)
        stacks.first()->data()->update_width(StackWidth::OneHalf, max.w, max.h);

    active = stacks.emplace_after(active, new Stack(window, max.w, max.h, mode));
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

    col->recalculate_stack_geometry(calculate_gap_x(c), gap, max);
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

    if (direction == ScrollerCore::local_item_backward_direction(mode))
        return active == stacks.first();
    if (direction == ScrollerCore::local_item_forward_direction(mode))
        return active == stacks.last();
    if (direction == ScrollerCore::stack_item_backward_direction(mode) ||
        direction == ScrollerCore::stack_item_forward_direction(mode))
        return active->data()->active_at_edge(direction);

    return false;
}

bool Lane::active_stack_has_multiple_windows() const {
    return active && active->data()->size() > 1;
}

// Execute directional focus movement inside this lane.
FocusMoveResult Lane::move_focus(Direction dir, bool focus_wrap) {
    if (!active)
        return FocusMoveResult::NoOp;

    reorder = Reorder::Auto;
    FocusMoveResult result = FocusMoveResult::NoOp;
    switch (dir) {
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
        if (dir == ScrollerCore::local_item_backward_direction(mode)) {
            result = move_focus_backward_stack(dir, focus_wrap);
        } else if (dir == ScrollerCore::local_item_forward_direction(mode)) {
            result = move_focus_forward_stack(dir, focus_wrap);
        } else if (dir == ScrollerCore::stack_item_backward_direction(mode) ||
                   dir == ScrollerCore::stack_item_forward_direction(mode)) {
            result = active->data()->move_focus(dir, focus_wrap);
        } else {
            return FocusMoveResult::NoOp;
        }
        break;
    }
    if (result != FocusMoveResult::Moved)
        return result;

    recalculate_lane_geometry();
    return result;
}

// Move focus to the previous stack, wrapping or crossing monitor when needed.
FocusMoveResult Lane::move_focus_backward_stack(Direction direction, bool focus_wrap) {
    if (active == stacks.first()) {
        const auto monitor = g_pCompositor->getMonitorInDirection(
            direction == Direction::Up ? Math::fromChar('u') : Math::fromChar('l'));
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
FocusMoveResult Lane::move_focus_forward_stack(Direction direction, bool focus_wrap) {
    if (active == stacks.last()) {
        const auto monitor = g_pCompositor->getMonitorInDirection(
            direction == Direction::Down ? Math::fromChar('d') : Math::fromChar('r'));
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

// Cycle the active stack span preset along the lane's primary axis.
void Lane::resize_active_stack(int step) {
    if (!active)
        return;

    if (active->data()->maximized())
        return;

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

    active->data()->resize_active_window(max, calculate_gap_x(active), gap, delta);
    recalculate_lane_geometry();
}

// Change the lane traversal mode used by focus and insertion logic.
void Lane::set_mode(Mode m) {
    if (mode == m)
        return;

    mode = m;
    for (auto stack = stacks.first(); stack != nullptr; stack = stack->next())
        stack->data()->set_mode(mode, max.w, max.h);
    reorder = Reorder::Auto;
    recalculate_lane_geometry();
}

// Align the active stack or active window against the current lane viewport.
void Lane::align_stack(Direction dir) {
    if (!active)
        return;

    if (active->data()->maximized() ||
        active->data()->fullscreen() ||
        active->data()->expanded())
        return;

    if (dir == ScrollerCore::local_item_backward_direction(mode)) {
        active->data()->set_geom_pos(max.x, max.y);
    } else if (dir == ScrollerCore::local_item_forward_direction(mode)) {
        if (mode == Mode::Column)
            active->data()->set_geom_pos(max.x, max.y + max.h - active->data()->get_geom_h());
        else
            active->data()->set_geom_pos(max.x + max.w - active->data()->get_geom_w(), max.y);
    } else {
        switch (dir) {
    case Direction::Center:
        if (mode == Mode::Column) {
            active->data()->align_window(Direction::Center, gap);
            active->data()->recalculate_stack_geometry(calculate_gap_x(active), gap, max);
        } else {
            center_active_stack();
        }
        break;
    case Direction::Left:
    case Direction::Right:
    case Direction::Up:
    case Direction::Down:
        active->data()->align_window(dir, gap);
        active->data()->recalculate_stack_geometry(calculate_gap_x(active), gap, max);
        break;
    default:
        return;
        }
    }
    reorder = Reorder::Lazy;
    recalculate_lane_geometry();
}

// Reorder stacks in row mode or windows in column mode.
void Lane::move_active_stack(Direction dir) {
    if (!active)
        return;

    if (dir == ScrollerCore::local_item_forward_direction(mode)) {
        if (active != stacks.last()) {
            auto next = active->next();
            stacks.swap(active, next);
        }
    } else if (dir == ScrollerCore::local_item_backward_direction(mode)) {
        if (active != stacks.first()) {
            auto prev = active->prev();
            stacks.swap(active, prev);
        }
    } else if (dir == ScrollerCore::stack_item_backward_direction(mode) ||
               dir == ScrollerCore::stack_item_forward_direction(mode)) {
        active->data()->move_active(dir);
    } else {
        switch (dir) {
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
        default:
            return;
        }
    }

    reorder = Reorder::Auto;
    recalculate_lane_geometry();
}

void Lane::move_active_window_to_adjacent_stack(Direction dir) {
    if (!active)
        return;

    const auto backward = ScrollerCore::local_item_backward_direction(mode);
    const auto forward = ScrollerCore::local_item_forward_direction(mode);
    if (dir != backward && dir != forward)
        return;

    auto *sourceStack = active->data();
    if (sourceStack->maximized() ||
        sourceStack->fullscreen())
        return;

    if (sourceStack->expanded())
        (void)sourceStack->toggle_fullscreen(max);

    auto *target = dir == backward ? active->prev() : active->next();
    if (!target)
        return;

    auto payload = extract_active_window_payload();
    const auto movedWindow = payload.window ? payload.window->ptr().lock() : nullptr;
    if (!payload)
        return;
    const auto targetWindowCountBefore = target->data()->size();

    active = target;
    active->data()->admit_window(payload.release_window());
    rememberWindowStack(movedWindow, active->data());

    reorder = Reorder::Auto;
    if (targetWindowCountBefore == 1)
        active->data()->fit_size(FitSize::All, calculate_gap_x(active), gap, max);
    recalculate_lane_geometry();
    debugVerifyStackCache();
}

void Lane::move_active_window_to_new_stack(Direction dir) {
    if (!active)
        return;

    const auto backward = ScrollerCore::local_item_backward_direction(mode);
    const auto forward = ScrollerCore::local_item_forward_direction(mode);
    if (dir != backward && dir != forward)
        return;

    auto *sourceStack = active->data();
    if (sourceStack->maximized() ||
        sourceStack->fullscreen() ||
        sourceStack->size() == 1)
        return;

    if (sourceStack->expanded())
        (void)sourceStack->toggle_fullscreen(max);

    auto *sourceNode = active;
    sourceStack = sourceNode->data();
    auto payload = extract_active_window_payload();
    const auto movedWindow = payload.window ? payload.window->ptr().lock() : nullptr;
    if (!payload)
        return;
    auto *stack = create_stack_from_payload(std::move(payload));
    if (!stack)
        return;

    ListNode<Stack *> *inserted = nullptr;
    if (dir == backward) {
        inserted = stacks.emplace_before(sourceNode, stack);
    } else {
        inserted = stacks.emplace_after(sourceNode, stack);
    }
    position_stack_relative_to_reference(stack, sourceStack, dir);

    active = inserted;
    rememberWindowStack(movedWindow, stack);
    reorder = Reorder::Auto;
    recalculate_lane_geometry();
    debugVerifyStackCache();
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

    auto prev = active->prev();
    auto payload = extract_active_window_payload();
    const auto movedWindow = payload.window ? payload.window->ptr().lock() : nullptr;
    if (!payload)
        return;
    const auto windowCountBefore = prev->data()->size();
    active = prev;
    active->data()->admit_window(payload.release_window());
    rememberWindowStack(movedWindow, active->data());

    reorder = Reorder::Auto;
    if (windowCountBefore == 1)
        active->data()->fit_size(FitSize::All, calculate_gap_x(active), gap, max);
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

    const auto forward = ScrollerCore::local_item_forward_direction(mode);
    auto *sourceStack = active->data();
    auto payload = extract_active_window_payload();
    const auto movedWindow = payload.window ? payload.window->ptr().lock() : nullptr;
    if (!payload)
        return;
    auto *stack = create_stack_from_payload(std::move(payload));
    if (!stack)
        return;
    active = stacks.emplace_after(active, stack);
    rememberWindowStack(movedWindow, active->data());
    position_stack_relative_to_reference(active->data(), sourceStack, forward);

    reorder = Reorder::Auto;
    recalculate_lane_geometry();
    debugVerifyStackCache();
}

// Fit stack sizes to the requested visible range along the lane's primary axis.
void Lane::fit_size(FitSize fitsize) {
    const auto visibleStart = mode == Mode::Column ? max.y : max.x;
    const auto visibleEnd = mode == Mode::Column ? max.y + max.h : max.x + max.w;
    const auto [from, to] = ScrollerCore::select_fit_size_range(
        fitsize,
        stacks.first(),
        stacks.last(),
        active,
        [&](ListNode<Stack *> *node) {
            auto *stack = node ? node->data() : nullptr;
            if (!stack)
                return false;

            const auto spanStart = mode == Mode::Column ? stack->get_geom_y() : stack->get_geom_x();
            const auto spanEnd = spanStart + (mode == Mode::Column ? stack->get_geom_h() : stack->get_geom_w());
            return ScrollerCore::Interval::intersects(spanStart, spanEnd, visibleStart, visibleEnd);
        });
    if (!ScrollerCore::normalize_fit_size_range(
            from,
            to,
            mode == Mode::Column ? max.h : max.w,
            [&](ListNode<Stack *> *node) {
                return mode == Mode::Column ? node->data()->get_geom_h() : node->data()->get_geom_w();
            },
            [&](ListNode<Stack *> *node, double scaledSpan) {
                auto *stack = node->data();
                stack->set_width_free();
                if (mode == Mode::Column)
                    stack->set_geom_h(scaledSpan);
                else
                    stack->set_geom_w(scaledSpan);
            }))
        return;

    from->data()->set_geom_pos(max.x, max.y);
    adjust_stacks(from);
}
