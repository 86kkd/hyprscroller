#include "row.h"

#include <cmath>
#include <sstream>

#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/config/ConfigManager.hpp>
#include <hyprland/src/config/ConfigValue.hpp>
#include <hyprland/src/plugins/PluginAPI.hpp>
#include <spdlog/spdlog.h>
#ifdef COLORS_IPC
#include <hyprland/src/managers/EventManager.hpp>
#endif

namespace {
namespace viewport {
bool column_intersects_visible_box(const Column *column, const ScrollerCore::Box &visible_box) {
    if (!column)
        return false;

    const auto left = column->get_geom_x();
    const auto right = left + column->get_geom_w();
    return left < visible_box.x + visible_box.w && left >= visible_box.x ||
           right > visible_box.x && right <= visible_box.x + visible_box.w ||
           left < visible_box.x && right >= visible_box.x + visible_box.w;
}

double choose_anchor_x(const ListNode<Column *> *active, const double active_width,
                       const double fallback_x, const ScrollerCore::Box &visible_box) {
    const auto next = active->next();
    const auto prev = active->prev();
    if (next) {
        const auto next_width = next->data()->get_geom_w();
        if (active_width + next_width <= visible_box.w)
            return visible_box.x + visible_box.w - active_width - next_width;
        if (prev && prev->data()->get_geom_w() + active_width <= visible_box.w)
            return visible_box.x + prev->data()->get_geom_w();
        if (!prev)
            return visible_box.x;
        return fallback_x;
    }
    if (prev) {
        if (prev->data()->get_geom_w() + active_width <= visible_box.w)
            return visible_box.x + prev->data()->get_geom_w();
        return visible_box.x + visible_box.w - active_width;
    }
    return fallback_x;
}
} // namespace viewport

namespace logging {
const void* active_window_ptr(Column *column) {
    if (!column)
        return nullptr;

    const auto window = column->get_active_window();
    return static_cast<const void*>(window ? window.get() : nullptr);
}

std::string summarize_columns(List<Column *>& columns) {
    std::ostringstream out;
    for (auto col = columns.first(); col != nullptr; col = col->next()) {
        if (col != columns.first())
            out << " | ";

        auto *data = col->data();
        out << active_window_ptr(data)
            << "@x=" << (data ? data->get_geom_x() : 0.0)
            << ",w=" << (data ? data->get_geom_w() : 0.0);
    }
    return out.str();
}
} // namespace logging
} // namespace

Vector2D Row::calculate_gap_x(const ListNode<Column *> *column) const {
    auto gap0 = column == columns.first() ? 0.0 : gap;
    auto gap1 = column == columns.last() ? 0.0 : gap;
    return Vector2D(gap0, gap1);
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

Vector2D Row::predict_window_size() const {
    if (mode == Mode::Column)
        return Vector2D(max.w, 0.5 * max.h);

    return Vector2D(0.5 * max.w, max.h);
}

void Row::update_sizes(PHLMONITOR monitor) {
    static auto PGAPSINDATA = CConfigValue<Hyprlang::CUSTOMTYPE>("general:gaps_in");
    static auto PGAPSOUTDATA = CConfigValue<Hyprlang::CUSTOMTYPE>("general:gaps_out");
    auto *const PGAPSIN = (CCssGapData *)(PGAPSINDATA.ptr())->getData();
    auto *const PGAPSOUT = (CCssGapData *)(PGAPSOUTDATA.ptr())->getData();
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
    active->data()->recalculate_col_geometry(calculate_gap_x(active), gap);
}

void Row::toggle_fullscreen_active_window() {
    Column *column = active->data();
    (void)column->toggle_fullscreen(max, mode);
    recalculate_row_geometry();
}

void Row::toggle_maximize_active_column() {
    Column *column = active->data();
    column->toggle_maximized(max.w, max.h);
    reorder = Reorder::Auto;
    recalculate_row_geometry();
}

void Row::toggle_overview() {
    overview = !overview;
    if (overview) {
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
                      logging::active_window_ptr(active->data()),
                      logging::summarize_columns(columns));
        return;
    }

    auto a_w = active->data()->get_geom_w();
    double a_x;
    if (active->data()->get_init()) {
        a_x = active->data()->get_geom_x();
    } else {
        if (active->prev()) {
            Column *prev = active->prev()->data();
            a_x = prev->get_geom_x() + prev->get_geom_w();
        } else if (active->next()) {
            a_x = active->data()->get_geom_x();
        } else {
            a_x = max.x + 0.5 * (max.w - a_w);
        }
        active->data()->set_init();
    }
    spdlog::debug("row_recalc_input: workspace={} active_window={} active_x={} active_w={} max=({}, {}, {}, {}) cols_before={}",
                  workspace,
                  logging::active_window_ptr(active->data()),
                  a_x,
                  a_w,
                  max.x,
                  max.y,
                  max.w,
                  max.h,
                  logging::summarize_columns(columns));
    if (a_x < max.x) {
        a_x = max.x;
        active->data()->set_geom_pos(max.x, max.y);
        adjust_columns(active);
        spdlog::debug("row_recalc_clamp_left: workspace={} active_window={} active_x={} cols_after={}",
                      workspace,
                      logging::active_window_ptr(active->data()),
                      active->data()->get_geom_x(),
                      logging::summarize_columns(columns));
        return;
    }
    if (std::round(a_x + a_w) > max.x + max.w) {
        a_x = max.x + max.w - a_w;
        active->data()->set_geom_pos(a_x, max.y);
        adjust_columns(active);
        spdlog::debug("row_recalc_clamp_right: workspace={} active_window={} active_x={} cols_after={}",
                      workspace,
                      logging::active_window_ptr(active->data()),
                      active->data()->get_geom_x(),
                      logging::summarize_columns(columns));
        return;
    }
    if (reorder != Reorder::Auto) {
        active->data()->set_geom_pos(a_x, max.y);
        adjust_columns(active);
        spdlog::debug("row_recalc_lazy: workspace={} active_window={} active_x={} cols_after={}",
                      workspace,
                      logging::active_window_ptr(active->data()),
                      active->data()->get_geom_x(),
                      logging::summarize_columns(columns));
        return;
    }

    const Box active_window(max.x, max.y, max.w, max.h);
    const bool prev_inside = viewport::column_intersects_visible_box(active->prev() ? active->prev()->data() : nullptr, active_window);
    const bool next_inside = viewport::column_intersects_visible_box(active->next() ? active->next()->data() : nullptr, active_window);
    const bool keep_current = prev_inside || next_inside;
    const double new_x = keep_current ? a_x : viewport::choose_anchor_x(active, a_w, a_x, max);
    active->data()->set_geom_pos(new_x, max.y);
    adjust_columns(active);
    spdlog::debug("row_recalc_auto: workspace={} active_window={} keep_current={} prev_inside={} next_inside={} new_x={} cols_after={}",
                  workspace,
                  logging::active_window_ptr(active->data()),
                  keep_current,
                  prev_inside,
                  next_inside,
                  new_x,
                  logging::summarize_columns(columns));
}

void Row::adjust_columns(ListNode<Column *> *column) {
    for (auto col = column->prev(), prev = column; col != nullptr; prev = col, col = col->prev()) {
        col->data()->set_geom_pos(prev->data()->get_geom_x() - col->data()->get_geom_w(), max.y);
        col->data()->set_init();
    }
    for (auto col = column->next(), prev = column; col != nullptr; prev = col, col = col->next()) {
        col->data()->set_geom_pos(prev->data()->get_geom_x() + prev->data()->get_geom_w(), max.y);
        col->data()->set_init();
    }

    column->data()->set_init();

    for (auto col = columns.first(); col != nullptr; col = col->next()) {
        auto gap0 = col == columns.first() ? 0.0 : gap;
        auto gap1 = col == columns.last() ? 0.0 : gap;
        col->data()->recalculate_col_geometry(Vector2D(gap0, gap1), gap);
    }
}
