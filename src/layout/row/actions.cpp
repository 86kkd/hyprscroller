#include "row.h"

#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/helpers/Monitor.hpp>

void Row::add_active_window(PHLWINDOW window) {
    if (mode == Mode::Column && active != nullptr) {
        const auto windowCountBefore = active->data()->size();
        active->data()->add_active_window(window, 0.5 * max.h);
        if (windowCountBefore == 1) {
            active->data()->fit_size(FitSize::All, calculate_gap_x(active), gap);
        } else {
            active->data()->recalculate_col_geometry(calculate_gap_x(active), gap);
        }
        return;
    }

    const bool singleWindowWorkspace = columns.size() == 1 && columns.first()->data()->size() == 1;
    if (singleWindowWorkspace)
        columns.first()->data()->update_width(ColumnWidth::OneHalf, max.w, max.h);

    active = columns.emplace_after(active, new Column(window, max.w, max.h));
    if (singleWindowWorkspace)
        active->data()->update_width(ColumnWidth::OneHalf, max.w, max.h);
    reorder = Reorder::Auto;
    recalculate_row_geometry();
}

bool Row::remove_window(PHLWINDOW window) {
    reorder = Reorder::Auto;
    for (auto c = columns.first(); c != nullptr; c = c->next()) {
        Column *col = c->data();
        if (!col->has_window(window))
            continue;

        col->remove_window(window);
        if (col->size() == 0) {
            if (c == active)
                active = active != columns.last() ? active->next() : active->prev();

            delete col;
            columns.erase(c);
            if (columns.empty())
                return false;

            recalculate_row_geometry();
            return true;
        }

        if (mode == Mode::Column) {
            if (c->data()->size() <= 2)
                c->data()->fit_size(FitSize::All, calculate_gap_x(c), gap);
            else
                c->data()->recalculate_col_geometry(calculate_gap_x(c), gap);
        } else {
            c->data()->recalculate_col_geometry(calculate_gap_x(c), gap);
        }
        return true;
    }
    return true;
}

bool Row::swapWindows(PHLWINDOW a, PHLWINDOW b) {
    ListNode<Column *> *ca = nullptr;
    ListNode<Column *> *cb = nullptr;

    for (auto c = columns.first(); c != nullptr; c = c->next()) {
        if (!ca && c->data()->has_window(a))
            ca = c;
        if (!cb && c->data()->has_window(b))
            cb = c;
    }

    if (!ca || !cb || ca != cb)
        return false;

    return ca->data()->swap_windows(a, b);
}

void Row::focus_window(PHLWINDOW window) {
    for (auto c = columns.first(); c != nullptr; c = c->next()) {
        if (!c->data()->has_window(window))
            continue;

        c->data()->focus_window(window);
        active = c;
        recalculate_row_geometry();
        return;
    }
}

FocusMoveResult Row::move_focus(Direction dir, bool focus_wrap) {
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
        if (active != columns.first()) {
            move_focus_begin();
            result = FocusMoveResult::Moved;
        }
        break;
    case Direction::End:
        if (active != columns.last()) {
            move_focus_end();
            result = FocusMoveResult::Moved;
        }
        break;
    default:
        return FocusMoveResult::NoOp;
    }
    if (result != FocusMoveResult::Moved)
        return result;

    recalculate_row_geometry();
    return result;
}

FocusMoveResult Row::move_focus_left(bool focus_wrap) {
    if (active == columns.first()) {
        PHLMONITOR monitor = g_pCompositor->getMonitorInDirection(Math::fromChar('l'));
        if (monitor == nullptr) {
            auto previous = active;
            if (focus_wrap)
                active = columns.last();
            return active != previous ? FocusMoveResult::Moved : FocusMoveResult::NoOp;
        }
        return FocusMoveResult::CrossMonitor;
    }
    active = active->prev();
    return FocusMoveResult::Moved;
}

FocusMoveResult Row::move_focus_right(bool focus_wrap) {
    if (active == columns.last()) {
        PHLMONITOR monitor = g_pCompositor->getMonitorInDirection(Math::fromChar('r'));
        if (monitor == nullptr) {
            auto previous = active;
            if (focus_wrap)
                active = columns.first();
            return active != previous ? FocusMoveResult::Moved : FocusMoveResult::NoOp;
        }
        return FocusMoveResult::CrossMonitor;
    }
    active = active->next();
    return FocusMoveResult::Moved;
}

void Row::move_focus_begin() {
    active = columns.first();
}

void Row::move_focus_end() {
    active = columns.last();
}

void Row::resize_active_column(int step) {
    if (active->data()->maximized())
        return;

    if (mode == Mode::Column) {
        active->data()->cycle_size_active_window(step, calculate_gap_x(active), gap);
        return;
    }

    ColumnWidth width = active->data()->get_width();
    if (width == ColumnWidth::Free) {
        width = ColumnWidth::OneHalf;
    } else {
        int number = static_cast<int>(ColumnWidth::Number);
        width = static_cast<ColumnWidth>((number + static_cast<int>(width) + step) % number);
    }
    active->data()->update_width(width, max.w, max.h);
    reorder = Reorder::Auto;
    recalculate_row_geometry();
}

void Row::resize_active_window(const Vector2D &delta) {
    if (active->data()->maximized() ||
        active->data()->fullscreen() ||
        active->data()->expanded())
        return;

    active->data()->resize_active_window(max.w, calculate_gap_x(active), gap, delta);
    recalculate_row_geometry();
}

void Row::set_mode(Mode m) {
    mode = m;
}

void Row::align_column(Direction dir) {
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
            active->data()->recalculate_col_geometry(calculate_gap_x(active), gap);
        } else {
            center_active_column();
        }
        break;
    case Direction::Up:
    case Direction::Down:
        active->data()->align_window(dir, gap);
        active->data()->recalculate_col_geometry(calculate_gap_x(active), gap);
        break;
    default:
        return;
    }
    reorder = Reorder::Lazy;
    recalculate_row_geometry();
}

void Row::move_active_column(Direction dir) {
    switch (dir) {
    case Direction::Right:
        if (active != columns.last()) {
            auto next = active->next();
            columns.swap(active, next);
        }
        break;
    case Direction::Left:
        if (active != columns.first()) {
            auto prev = active->prev();
            columns.swap(active, prev);
        }
        break;
    case Direction::Up:
        active->data()->move_active_up();
        break;
    case Direction::Down:
        active->data()->move_active_down();
        break;
    case Direction::Begin:
        if (active != columns.first())
            columns.move_before(columns.first(), active);
        break;
    case Direction::End:
        if (active != columns.last())
            columns.move_after(columns.last(), active);
        break;
    case Direction::Center:
        return;
    }

    reorder = Reorder::Auto;
    recalculate_row_geometry();
}

void Row::admit_window_left() {
    if (active->data()->maximized() ||
        active->data()->fullscreen() ||
        active->data()->expanded() ||
        active == columns.first())
        return;

    auto w = active->data()->expel_active(gap);
    auto prev = active->prev();
    if (active->data()->size() == 0)
        columns.erase(active);
    active = prev;
    active->data()->admit_window(w);

    reorder = Reorder::Auto;
    recalculate_row_geometry();
}

void Row::expel_window_right() {
    if (active->data()->maximized() ||
        active->data()->fullscreen() ||
        active->data()->expanded() ||
        active->data()->size() == 1)
        return;

    auto w = active->data()->expel_active(gap);
    ColumnWidth width = active->data()->get_width();
    double maxw = width == ColumnWidth::Free ? active->data()->get_geom_w() : max.w;
    active = columns.emplace_after(active, new Column(w, width, maxw, max.h));
    active->data()->set_geom_pos(active->prev()->data()->get_geom_x() + active->prev()->data()->get_geom_w(), max.y);

    reorder = Reorder::Auto;
    recalculate_row_geometry();
}

void Row::fit_size(FitSize fitsize) {
    if (mode == Mode::Column) {
        active->data()->fit_size(fitsize, calculate_gap_x(active), gap);
        return;
    }
    ListNode<Column *> *from, *to;
    switch (fitsize) {
    case FitSize::Active:
        from = to = active;
        break;
    case FitSize::Visible:
        for (auto c = columns.first(); c != nullptr; c = c->next()) {
            Column *col = c->data();
            auto c0 = col->get_geom_x();
            auto c1 = col->get_geom_x() + col->get_geom_w();
            if (c0 < max.x + max.w && c0 >= max.x ||
                c1 > max.x && c1 <= max.x + max.w ||
                c0 < max.x && c1 >= max.x + max.w) {
                from = c;
                break;
            }
        }
        for (auto c = columns.last(); c != nullptr; c = c->prev()) {
            Column *col = c->data();
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
        from = columns.first();
        to = columns.last();
        break;
    case FitSize::ToEnd:
        from = active;
        to = columns.last();
        break;
    case FitSize::ToBeg:
        from = columns.first();
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
            Column *col = c->data();
            col->set_width_free();
            col->set_geom_w(col->get_geom_w() / total * max.w);
        }
        from->data()->set_geom_pos(max.x, max.y);
        adjust_columns(from);
    }
}
