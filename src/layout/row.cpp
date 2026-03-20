#include "row.h"

#include <algorithm>
#include <cmath>
#include <sstream>

#include <hyprland/src/config/ConfigManager.hpp>
#include <hyprland/src/config/ConfigValue.hpp>
#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/helpers/Monitor.hpp>
#include <hyprland/src/managers/KeybindManager.hpp>
#include <spdlog/spdlog.h>
#ifdef COLORS_IPC
#include <hyprland/src/managers/EventManager.hpp>
#endif

namespace {
static bool is_left_or_right_inside(const Column *col, const ScrollerCore::Box &box) {
    if (!col)
        return false;
    const auto x0 = col->get_geom_x();
    const auto x1 = x0 + col->get_geom_w();
    return x0 < box.x + box.w && x0 >= box.x ||
           x1 > box.x && x1 <= box.x + box.w ||
           x0 < box.x && x1 >= box.x + box.w;
}

static double choose_anchor_x(const ListNode<Column *> *active, const double active_w,
                            const double fallback_x, const ScrollerCore::Box &max_box) {
    const auto next = active->next();
    const auto prev = active->prev();
    if (next) {
        const auto next_w = next->data()->get_geom_w();
        if (active_w + next_w <= max_box.w)
            return max_box.x + max_box.w - active_w - next_w;
        if (prev && prev->data()->get_geom_w() + active_w <= max_box.w)
            return max_box.x + prev->data()->get_geom_w();
        if (!prev)
            return max_box.x;
        return fallback_x;
    }
    if (prev) {
        if (prev->data()->get_geom_w() + active_w <= max_box.w)
            return max_box.x + prev->data()->get_geom_w();
        return max_box.x + max_box.w - active_w;
    }
    return fallback_x;
}

static std::string summarize_columns(List<Column *>& columns) {
    std::ostringstream out;
    for (auto col = columns.first(); col != nullptr; col = col->next()) {
        if (col != columns.first())
            out << " | ";

        auto* data = col->data();
        const auto window = data ? data->get_active_window() : nullptr;
        out << static_cast<const void*>(window ? window.get() : nullptr)
            << "@x=" << (data ? data->get_geom_x() : 0.0)
            << ",w=" << (data ? data->get_geom_w() : 0.0);
    }
    return out.str();
}
} // namespace

Row::Row(PHLWINDOW window)
    : workspace(window->workspaceID()), mode(Mode::Row), reorder(Reorder::Auto),
      overview(false), active(nullptr) {
    const auto monitor = g_pCompositor->getMonitorFromID(window->monitorID());
    if (!monitor)
        return;

    mode = monitor->m_size.x >= monitor->m_size.y ? Mode::Row : Mode::Column;
    update_sizes(monitor);
}

Row::~Row() {
    for (auto col = columns.first(); col != nullptr; col = col->next()) {
        delete col->data();
    }
    columns.clear();
}

int Row::get_workspace() const {
    return workspace;
}

bool Row::has_window(PHLWINDOW window) const {
    for (auto col = columns.first(); col != nullptr; col = col->next()) {
        if (col->data()->has_window(window))
            return true;
    }
    return false;
}

PHLWINDOW Row::get_active_window() const {
    return active->data()->get_active_window();
}

bool Row::is_active(PHLWINDOW window) const {
    return get_active_window() == window;
}

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
    if (singleWindowWorkspace) {
        columns.first()->data()->update_width(ColumnWidth::OneHalf, max.w, max.h);
    }

    active = columns.emplace_after(active, new Column(window, max.w, max.h));
    if (singleWindowWorkspace) {
        active->data()->update_width(ColumnWidth::OneHalf, max.w, max.h);
    }
    reorder = Reorder::Auto;
    recalculate_row_geometry();
}

bool Row::remove_window(PHLWINDOW window) {
    reorder = Reorder::Auto;
    for (auto c = columns.first(); c != nullptr; c = c->next()) {
        Column *col = c->data();
        if (col->has_window(window)) {
            col->remove_window(window);
            if (col->size() == 0) {
                if (c == active) {
                    // make NEXT one active before deleting (like PaperWM)
                    // If active was the only one left, doesn't matter
                    // whether it points to end() or not, the row will
                    // be deleted by the parent.
                    active = active != columns.last() ? active->next() : active->prev();
                }
                delete col;
                columns.erase(c);
                if (columns.empty()) {
                    return false;
                } else {
                    recalculate_row_geometry();
                    return true;
                }
            } else {
                if (mode == Mode::Column) {
                    if (c->data()->size() <= 2) {
                        c->data()->fit_size(FitSize::All, calculate_gap_x(c), gap);
                    } else {
                        c->data()->recalculate_col_geometry(calculate_gap_x(c), gap);
                    }
                } else {
                    c->data()->recalculate_col_geometry(calculate_gap_x(c), gap);
                }
                return true;
            }
        }
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
        if (c->data()->has_window(window)) {
            c->data()->focus_window(window);
            active = c;
            recalculate_row_geometry();
            return;
        }
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

Vector2D Row::calculate_gap_x(const ListNode<Column *> *column) const {
    // First and last columns need a different gap.
    auto gap0 = column == columns.first() ? 0.0 : gap;
    auto gap1 = column == columns.last() ? 0.0 : gap;
    return Vector2D(gap0, gap1);
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
        // When cycle-resizing from Free mode, always move back to OneHalf.
        width = ColumnWidth::OneHalf;
    } else {
        int number = static_cast<int>(ColumnWidth::Number);
        width = static_cast<ColumnWidth>(
                (number + static_cast<int>(width) + step) % number);
    }
    active->data()->update_width(width, max.w, max.h);
    reorder = Reorder::Auto;
    recalculate_row_geometry();
}

void Row::resize_active_window(const Vector2D &delta) {
    // If the active window in the active column is fullscreen, ignore.
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

void Row::center_active_column() {
    Column *column = active->data();
    if (column->maximized())
        return;

    switch (column->get_width()) {
    case ColumnWidth::OneThird:
        column->set_geom_pos(max.x + max.w / 3.0, max.y);
        break;
    case ColumnWidth::OneHalf:
        column->set_geom_pos(max.x + max.w / 4.0, max.y);
        break;
    case ColumnWidth::TwoThirds:
        column->set_geom_pos(max.x + max.w / 6.0, max.y);
        break;
    case ColumnWidth::Free:
        column->set_geom_pos(0.5 * (max.w - column->get_geom_w()), max.y);
        break;
    default:
        break;
    }
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
    case Direction::Begin: {
        if (active == columns.first())
            break;
        columns.move_before(columns.first(), active);
        break;
    }
    case Direction::End: {
        if (active == columns.last())
            break;
        columns.move_after(columns.last(), active);
        break;
    }
    case Direction::Center:
        return;
    }

    reorder = Reorder::Auto;
    recalculate_row_geometry();
}

void Row::admit_window_left() {
    if (active->data()->maximized() ||
        active->data()->fullscreen() ||
        active->data()->expanded())
        return;
    if (active == columns.first())
        return;

    auto w = active->data()->expel_active(gap);
    auto prev = active->prev();
    if (active->data()->size() == 0) {
        columns.erase(active);
    }
    active = prev;
    active->data()->admit_window(w);

    reorder = Reorder::Auto;
    recalculate_row_geometry();
}

void Row::expel_window_right() {
    if (active->data()->maximized() ||
        active->data()->fullscreen() ||
        active->data()->expanded())
        return;
    if (active->data()->size() == 1)
        // nothing to expel
        return;

    auto w = active->data()->expel_active(gap);
    ColumnWidth width = active->data()->get_width();
    // This code inherits the width of the original column. There is a
    // problem with that when the mode is "Free". The new column may have
    // more reserved space for gaps, and the new window in that column
    // end up having negative size --> crash.
    // There are two options:
    // 1. We don't let column resizing make a column smaller than gap
    // 2. We compromise and inherit the ColumnWidth attribute unless it is
    // "Free". In that case, we force OneHalf (the default).
    double maxw = width == ColumnWidth::Free ? active->data()->get_geom_w() : max.w;
    active = columns.emplace_after(active, new Column(w, width, maxw, max.h));
    // Initialize the position so it is located after its previous column.
    // This helps the heuristic in recalculate_row_geometry().
    active->data()->set_geom_pos(active->prev()->data()->get_geom_x() + active->prev()->data()->get_geom_w(), max.y);

    reorder = Reorder::Auto;
    recalculate_row_geometry();
}

Vector2D Row::predict_window_size() const {
    if (mode == Mode::Column)
        return Vector2D(max.w, 0.5 * max.h);

    return Vector2D(0.5 * max.w, max.h);
}

void Row::update_sizes(PHLMONITOR monitor) {
    // for gaps outer
    static auto PGAPSINDATA = CConfigValue<Hyprlang::CUSTOMTYPE>("general:gaps_in");
    static auto PGAPSOUTDATA = CConfigValue<Hyprlang::CUSTOMTYPE>("general:gaps_out");
    auto *const PGAPSIN = (CCssGapData *)(PGAPSINDATA.ptr())->getData();
    auto *const PGAPSOUT = (CCssGapData *)(PGAPSOUTDATA.ptr())->getData();
    // For now, support only constant CCssGapData.
    auto gaps_in = PGAPSIN->m_top;
    auto gaps_out = PGAPSOUT->m_top;

    const auto reserved = monitor->m_reservedArea;
    const auto gapOutTopLeft = Vector2D(reserved.left(), reserved.top());
    const auto gapOutBottomRight = Vector2D(reserved.right(), reserved.bottom());
    const auto size = Vector2D(monitor->m_size.x, monitor->m_size.y);
    const auto pos = Vector2D(monitor->m_position.x, monitor->m_position.y);

    full = Box(pos, size);
    max = Box(pos.x + gapOutTopLeft.x + gaps_out,
              pos.y + gapOutTopLeft.y + gaps_out,
              size.x - gapOutTopLeft.x - gapOutBottomRight.x - 2 * gaps_out,
              size.y - gapOutTopLeft.y - gapOutBottomRight.y - 2 * gaps_out);
    gap = gaps_in;
}

void Row::set_fullscreen_active_window() {
    active->data()->set_fullscreen(full);
    // Parameters here don't matter.
    active->data()->recalculate_col_geometry(calculate_gap_x(active), gap);
}

void Row::toggle_fullscreen_active_window() {
    Column *column = active->data();
    (void)column->toggle_fullscreen(full, mode);
    recalculate_row_geometry();
}

void Row::toggle_maximize_active_column() {
    Column *column = active->data();
    column->toggle_maximized(max.w, max.h);
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
                // should never happen as columns are never wider than the screen
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
                // should never happen as columns are never wider than the screen
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

    // Now align from to left edge of the screen (max.x), split width of
    // screen (max.w) among from->to, and readapt the rest.
    if (from != nullptr && to != nullptr) {
        auto c = from;
        double total = 0.0;
        for (auto c = from; c != to->next(); c = c->next()) {
            total += c->data()->get_geom_w();
        }
        for (auto c = from; c != to->next(); c = c->next()) {
            Column *col = c->data();
            col->set_width_free();
            col->set_geom_w(col->get_geom_w() / total * max.w);
        }
        from->data()->set_geom_pos(max.x, max.y);

        adjust_columns(from);
    }
}

void Row::toggle_overview() {
    overview = !overview;
    if (overview) {
        // Find the bounding box.
        Vector2D bmin(max.x + max.w, max.y + max.h);
        Vector2D bmax(max.x, max.y);
        for (auto c = columns.first(); c != nullptr; c = c->next()) {
            auto cx0 = c->data()->get_geom_x();
            auto cx1 = cx0 + c->data()->get_geom_w();
            Vector2D cheight = c->data()->get_height();
            if (cx0 < bmin.x)
                bmin.x = cx0;
            if (cx1 > bmax.x)
                bmax.x = cx1;
            if (cheight.x < bmin.y)
                bmin.y = cheight.x;
            if (cheight.y > bmax.y)
                bmax.y = cheight.y;
        }
        double w = bmax.x - bmin.x;
        double h = bmax.y - bmin.y;
        double scale = std::min(max.w / w, max.h / h);
        for (auto c = columns.first(); c != nullptr; c = c->next()) {
            Column *col = c->data();
            col->push_geom();
            Vector2D cheight = col->get_height();
            Vector2D offset(0.5 * (max.w - w * scale), 0.5 * (max.h - h * scale));
            col->set_geom_pos(offset.x + max.x + (col->get_geom_x() - bmin.x) * scale, offset.y + max.y + (cheight.x - bmin.y) * scale);
            col->set_geom_w(col->get_geom_w() * scale);
            Vector2D start(offset.x + max.x, offset.y + max.y);
            col->scale(bmin, start, scale, gap);
        }
        adjust_columns(columns.first());
    } else {
        for (auto c = columns.first(); c != nullptr; c = c->next()) {
            Column *col = c->data();
            col->pop_geom();
        }
        // Try to maintain the positions except if the active is not visible,
        // in that case, make it visible.
        Column *acolumn = active->data();
        if (acolumn->get_geom_x() < max.x) {
            acolumn->set_geom_pos(max.x, max.y);
        } else if (acolumn->get_geom_x() + acolumn->get_geom_w() > max.x + max.w) {
            acolumn->set_geom_pos(max.x + max.w - acolumn->get_geom_w(), max.y);
        }
        adjust_columns(active);
    }
}

void Row::recalculate_row_geometry() {
    if (active == nullptr)
        return;

    if (const auto activeWindow = active->data()->get_active_window(); activeWindow && activeWindow->isFullscreen()) {
        active->data()->recalculate_col_geometry(calculate_gap_x(active), gap);
        return;
    }
#ifdef COLORS_IPC
    // Change border color.
    static auto *const FREECOLUMN = (CGradientValueData *) HyprlandAPI::getConfigValue(PHANDLE, "plugin:scroller:col.freecolumn_border")->data.get();
    static auto *const ACTIVECOL = (CGradientValueData *)g_pConfigManager->getConfigValuePtr("general:col.active_border")->data.get();
    if (active->data()->get_width() == ColumnWidth::Free) {
        active->data()->get_active_window()->m_cRealBorderColor = *FREECOLUMN;
    } else {
        active->data()->get_active_window()->m_cRealBorderColor = *ACTIVECOL;
    }
    g_pEventManager->postEvent(SHyprIPCEvent{"scroller", active->data()->get_width_name() + "," + active->data()->get_height_name()});
#endif
    if (columns.size() == 1 && active->data()->size() == 1) {
        active->data()->set_geom_pos(max.x, max.y);
        active->data()->set_geom_w(max.w);
        active->data()->recalculate_col_geometry(calculate_gap_x(active), gap);
        spdlog::debug("row_recalc_single: workspace={} active_window={} cols={}",
                      workspace,
                      static_cast<const void*>(active->data()->get_active_window() ? active->data()->get_active_window().get() : nullptr),
                      summarize_columns(columns));
        return;
    }

    auto a_w = active->data()->get_geom_w();
    double a_x;
    if (active->data()->get_init()) {
        a_x = active->data()->get_geom_x();
    } else {
        // If the column hasn't been initialized yet (newly created window),
        // we know it will be located on the right of active->prev()
        if (active->prev()) {
            // there is a previous one, locate it on its right
            Column *prev = active->prev()->data();
            a_x = prev->get_geom_x() + prev->get_geom_w();
        } else if (active->next()) {
            // A first column in a multi-column row has already been positioned
            // by previous row adjustments. Reusing its current x avoids
            // re-centering it as if it were the only column.
            a_x = active->data()->get_geom_x();
        } else {
            // first window, locate it at the center
            a_x = max.x + 0.5 * (max.w - a_w);
        }
        // mark column as initialized
        active->data()->set_init();
    }
    spdlog::debug("row_recalc_input: workspace={} active_window={} active_x={} active_w={} max=({}, {}, {}, {}) cols_before={}",
                  workspace,
                  static_cast<const void*>(active->data()->get_active_window() ? active->data()->get_active_window().get() : nullptr),
                  a_x,
                  a_w,
                  max.x,
                  max.y,
                  max.w,
                  max.h,
                  summarize_columns(columns));
    if (a_x < max.x) {
        a_x = max.x;
        active->data()->set_geom_pos(max.x, max.y);
        adjust_columns(active);
        spdlog::debug("row_recalc_clamp_left: workspace={} active_window={} active_x={} cols_after={}",
                      workspace,
                      static_cast<const void*>(active->data()->get_active_window() ? active->data()->get_active_window().get() : nullptr),
                      active->data()->get_geom_x(),
                      summarize_columns(columns));
        return;
    }
    if (std::round(a_x + a_w) > max.x + max.w) {
        a_x = max.x + max.w - a_w;
        active->data()->set_geom_pos(a_x, max.y);
        adjust_columns(active);
        spdlog::debug("row_recalc_clamp_right: workspace={} active_window={} active_x={} cols_after={}",
                      workspace,
                      static_cast<const void*>(active->data()->get_active_window() ? active->data()->get_active_window().get() : nullptr),
                      active->data()->get_geom_x(),
                      summarize_columns(columns));
        return;
    }
    if (reorder != Reorder::Auto) {
        // lazy: avoid moving the active column unless it is out of screen.
        active->data()->set_geom_pos(a_x, max.y);
        adjust_columns(active);
        spdlog::debug("row_recalc_lazy: workspace={} active_window={} active_x={} cols_after={}",
                      workspace,
                      static_cast<const void*>(active->data()->get_active_window() ? active->data()->get_active_window().get() : nullptr),
                      active->data()->get_geom_x(),
                      summarize_columns(columns));
        return;
    }

    const Box active_window(max.x, max.y, max.w, max.h);
    const bool prev_inside = is_left_or_right_inside(active->prev() ? active->prev()->data() : nullptr, active_window);
    const bool next_inside = is_left_or_right_inside(active->next() ? active->next()->data() : nullptr, active_window);
    const bool keep_current = prev_inside || next_inside;
    const double new_x = keep_current ? a_x : choose_anchor_x(active, a_w, a_x, max);
    active->data()->set_geom_pos(new_x, max.y);
    adjust_columns(active);
    spdlog::debug("row_recalc_auto: workspace={} active_window={} keep_current={} prev_inside={} next_inside={} new_x={} cols_after={}",
                  workspace,
                  static_cast<const void*>(active->data()->get_active_window() ? active->data()->get_active_window().get() : nullptr),
                  keep_current,
                  prev_inside,
                  next_inside,
                  new_x,
                  summarize_columns(columns));
}

void Row::adjust_columns(ListNode<Column *> *column) {
    // Adjust the positions of the columns to the left.
    for (auto col = column->prev(), prev = column; col != nullptr; prev = col, col = col->prev()) {
        col->data()->set_geom_pos(prev->data()->get_geom_x() - col->data()->get_geom_w(), max.y);
        col->data()->set_init();
    }
    // Adjust the positions of the columns to the right.
    for (auto col = column->next(), prev = column; col != nullptr; prev = col, col = col->next()) {
        col->data()->set_geom_pos(prev->data()->get_geom_x() + prev->data()->get_geom_w(), max.y);
        col->data()->set_init();
    }

    column->data()->set_init();

    // Apply column geometry.
    for (auto col = columns.first(); col != nullptr; col = col->next()) {
        // First and last columns need a different gap.
        auto gap0 = col == columns.first() ? 0.0 : gap;
        auto gap1 = col == columns.last() ? 0.0 : gap;
        col->data()->recalculate_col_geometry(Vector2D(gap0, gap1), gap);
    }
}
