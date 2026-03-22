#include "lane.h"

#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/helpers/Monitor.hpp>

void Lane::add_active_window(PHLWINDOW window) {
    if (mode == Mode::Column && active != nullptr) {
        const auto windowCountBefore = active->data()->size();
        active->data()->add_active_window(window, 0.5 * max.h);
        if (windowCountBefore == 1) {
            active->data()->fit_size(FitSize::All, calculate_gap_x(active), gap);
        } else {
            active->data()->recalculate_stack_geometry(calculate_gap_x(active), gap);
        }
        return;
    }

    const bool singleWindowWorkspace = stacks.size() == 1 && stacks.first()->data()->size() == 1;
    if (singleWindowWorkspace)
        stacks.first()->data()->update_width(StackWidth::OneHalf, max.w, max.h);

    active = stacks.emplace_after(active, new Stack(window, max.w, max.h));
    if (singleWindowWorkspace)
        active->data()->update_width(StackWidth::OneHalf, max.w, max.h);
    reorder = Reorder::Auto;
    recalculate_lane_geometry();
}

bool Lane::remove_window(PHLWINDOW window) {
    reorder = Reorder::Auto;
    for (auto c = stacks.first(); c != nullptr; c = c->next()) {
        Stack *col = c->data();
        if (!col->has_window(window))
            continue;

        col->remove_window(window);
        if (col->size() == 0) {
            if (c == active)
                active = active != stacks.last() ? active->next() : active->prev();

            delete col;
            stacks.erase(c);
            if (stacks.empty())
                return false;

            recalculate_lane_geometry();
            return true;
        }

        if (mode == Mode::Column) {
            if (c->data()->size() <= 2)
                c->data()->fit_size(FitSize::All, calculate_gap_x(c), gap);
            else
                c->data()->recalculate_stack_geometry(calculate_gap_x(c), gap);
        } else {
            c->data()->recalculate_stack_geometry(calculate_gap_x(c), gap);
        }
        return true;
    }
    return true;
}

bool Lane::swapWindows(PHLWINDOW a, PHLWINDOW b) {
    ListNode<Stack *> *ca = nullptr;
    ListNode<Stack *> *cb = nullptr;

    for (auto c = stacks.first(); c != nullptr; c = c->next()) {
        if (!ca && c->data()->has_window(a))
            ca = c;
        if (!cb && c->data()->has_window(b))
            cb = c;
    }

    if (!ca || !cb || ca != cb)
        return false;

    return ca->data()->swap_windows(a, b);
}

void Lane::focus_window(PHLWINDOW window) {
    for (auto c = stacks.first(); c != nullptr; c = c->next()) {
        if (!c->data()->has_window(window))
            continue;

        c->data()->focus_window(window);
        active = c;
        recalculate_lane_geometry();
        return;
    }
}

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

void Lane::move_focus_begin() {
    active = stacks.first();
}

void Lane::move_focus_end() {
    active = stacks.last();
}

void Lane::resize_active_stack(int step) {
    if (!active)
        return;

    if (active->data()->maximized())
        return;

    if (mode == Mode::Column) {
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

void Lane::set_mode(Mode m) {
    mode = m;
}

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
        if (mode == Mode::Column) {
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

void Lane::admit_window_left() {
    if (!active)
        return;

    if (active->data()->maximized() ||
        active->data()->fullscreen() ||
        active->data()->expanded() ||
        active == stacks.first())
        return;

    auto w = active->data()->expel_active(gap);
    auto prev = active->prev();
    if (active->data()->size() == 0)
        stacks.erase(active);
    active = prev;
    active->data()->admit_window(w);

    reorder = Reorder::Auto;
    recalculate_lane_geometry();
}

void Lane::expel_window_right() {
    if (active->data()->maximized() ||
        active->data()->fullscreen() ||
        active->data()->expanded() ||
        active->data()->size() == 1)
        return;

    auto w = active->data()->expel_active(gap);
    StackWidth width = active->data()->get_width();
    double maxw = width == StackWidth::Free ? active->data()->get_geom_w() : max.w;
    active = stacks.emplace_after(active, new Stack(w, width, maxw, max.h));
    active->data()->set_geom_pos(active->prev()->data()->get_geom_x() + active->prev()->data()->get_geom_w(), max.y);

    reorder = Reorder::Auto;
    recalculate_lane_geometry();
}

void Lane::fit_size(FitSize fitsize) {
    if (mode == Mode::Column) {
        active->data()->fit_size(fitsize, calculate_gap_x(active), gap);
        return;
    }
    ListNode<Stack *> *from, *to;
    switch (fitsize) {
    case FitSize::Active:
        from = to = active;
        break;
    case FitSize::Visible:
        for (auto c = stacks.first(); c != nullptr; c = c->next()) {
            Stack *col = c->data();
            auto c0 = col->get_geom_x();
            auto c1 = col->get_geom_x() + col->get_geom_w();
            if (c0 < max.x + max.w && c0 >= max.x ||
                c1 > max.x && c1 <= max.x + max.w ||
                c0 < max.x && c1 >= max.x + max.w) {
                from = c;
                break;
            }
        }
        for (auto c = stacks.last(); c != nullptr; c = c->prev()) {
            Stack *col = c->data();
            auto c0 = col->get_geom_x();
            auto c1 = col->get_geom_x() + col->get_geom_w();
            if (c0 < max.x + max.w && c0 >= max.x ||
                c1 > max.x && c1 <= max.x + max.w ||
                c0 < max.x && c1 >= max.x + max.w) {
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

        for (auto c = from; c != to->next(); c = c->next()) {
            Stack *col = c->data();
            col->set_width_free();
            col->set_geom_w(col->get_geom_w() / total * max.w);
        }
        from->data()->set_geom_pos(max.x, max.y);
        adjust_stacks(from);
    }
}
