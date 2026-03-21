#include "model.h"

#include <algorithm>
#include <cmath>

#include <hyprlang.hpp>
#include <spdlog/spdlog.h>
#include <hyprland/src/config/ConfigManager.hpp>
#include <hyprland/src/config/ConfigValue.hpp>
#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/managers/KeybindManager.hpp>
#include <hyprland/src/plugins/PluginAPI.hpp>
#include <hyprland/src/layout/space/Space.hpp>
#include <hyprland/src/layout/target/Target.hpp>

extern HANDLE PHANDLE;

namespace ScrollerModel {
namespace {
static bool is_window_fully_visible(Window *window, double gap, const ScrollerCore::Box &geom) {
    if (!window)
        return false;
    const auto y0 = std::round(window->get_geom_y());
    const auto y1 = std::round(window->get_geom_y() + window->get_geom_h());
    return y0 >= geom.y && y1 <= geom.y + geom.h;
}

static bool is_window_intersect_viewport(Window *window, double gap, const ScrollerCore::Box &geom) {
    if (!window)
        return false;
    const auto y0 = window->get_geom_y();
    const auto y1 = window->get_geom_y() + window->get_geom_h();
    return y0 < geom.y + geom.h && y0 >= geom.y ||
           y1 > geom.y && y1 <= geom.y + geom.h ||
           y0 < geom.y && y1 >= geom.y + geom.h;
}

static double window_active_x(const ScrollerCore::Box &geom, double border_x, double gap_x) {
    return geom.x + border_x + gap_x;
}

static double choose_anchor_y(bool has_next, bool has_prev, double active_h, double next_h, double prev_h,
                             const ScrollerCore::Box &geom) {
    const auto base_y = geom.y;
    const auto stack_to_bottom = geom.y + geom.h - active_h;
    if (has_next && active_h + next_h <= geom.h) {
        return geom.y + geom.h - active_h - next_h;
    }
    if (has_next && has_prev && prev_h + active_h <= geom.h) {
        return geom.y + prev_h;
    }
    if (!has_next && has_prev && prev_h + active_h <= geom.h) {
        return geom.y + prev_h;
    }
    if (!has_next && has_prev) {
        return stack_to_bottom;
    }
    return base_y;
}

static void sync_window_target_geometry(PHLWINDOW window) {
    if (!window)
        return;

    const auto target = window->layoutTarget();
    if (!target)
        return;

    target->setPositionGlobal(Hyprutils::Math::CBox(window->m_position, window->m_size));
}
} // namespace

Column::Column(PHLWINDOW cwindow, double maxw, double maxh)
    : height(WindowHeight::One), reorder(Reorder::Auto), initialized(false), maxdim(false) {
    static auto const *column_default_width = (Hyprlang::STRING const *)HyprlandAPI::getConfigValue(PHANDLE, "plugin:scroller:column_default_width")->getDataStaticPtr();
    std::string column_width = *column_default_width;
    if (column_width == "onehalf") {
        width = ColumnWidth::OneHalf;
    } else if (column_width == "onethird") {
        width = ColumnWidth::OneThird;
    } else if (column_width == "twothirds") {
        width = ColumnWidth::TwoThirds;
    } else if (column_width == "maximized") {
        width = ColumnWidth::Free;
    } else if (column_width == "floating") {
        auto target = cwindow->layoutTarget();
        if (target && target->lastFloatingSize().y > 0) {
            width = ColumnWidth::Free;
            maxw = target->lastFloatingSize().x;
        } else {
            width = ColumnWidth::OneHalf;
        }
    } else {
        width = ColumnWidth::OneHalf;
    }

    Window *window = new Window(cwindow, maxh);
    update_width(width, maxw, maxh);
    geom.h = maxh;
    windows.push_back(window);
    active = windows.first();
}

Column::Column(Window *window, ColumnWidth width, double maxw, double maxh)
    : width(width), height(WindowHeight::One), reorder(Reorder::Auto), initialized(true), maxdim(false) {
    window->set_geom_h(maxh);
    update_width(width, maxw, maxh);
    geom.h = maxh;
    windows.push_back(window);
    active = windows.first();
}

Column::~Column() {
    for (auto win = windows.first(); win != nullptr; win = win->next()) {
        delete win->data();
    }
    windows.clear();
}

bool Column::get_init() const {
    return initialized;
}

void Column::set_init() {
    initialized = true;
}

size_t Column::size() {
    return windows.size();
}

bool Column::has_window(PHLWINDOW window) const {
    for (auto win = windows.first(); win != nullptr; win = win->next()) {
        if (win->data()->ptr().lock() == window)
            return true;
    }
    return false;
}

bool Column::swap_windows(PHLWINDOW a, PHLWINDOW b) {
    if (a == b)
        return false;

    ListNode<Window *> *na = nullptr;
    ListNode<Window *> *nb = nullptr;
    for (auto win = windows.first(); win != nullptr; win = win->next()) {
        const auto w = win->data()->ptr().lock();
        if (w == a)
            na = win;
        else if (w == b)
            nb = win;
        if (na && nb)
            break;
    }
    if (!na || !nb)
        return false;

    windows.swap(na, nb);
    if (active == na)
        active = nb;
    else if (active == nb)
        active = na;
    return true;
}

void Column::add_active_window(PHLWINDOW window, double maxh) {
    reorder = Reorder::Auto;
    const auto previous = active;
    const auto new_height = previous ? previous->data()->get_geom_h() : maxh;
    active = windows.emplace_after(active, new Window(window, new_height));

    if (!previous)
        return;

    active->data()->set_geom_y(previous->data()->get_geom_y() + previous->data()->get_geom_h());
}

void Column::remove_window(PHLWINDOW window) {
    reorder = Reorder::Auto;
    for (auto win = windows.first(); win != nullptr; win = win->next()) {
        if (win->data()->ptr().lock() == window) {
            if (window == active->data()->ptr().lock()) {
                active = active != windows.last() ? active->next() : active->prev();
            }
            windows.erase(win);
            if (windows.size() == 1 && active) {
                active->data()->update_height(WindowHeight::One, geom.h);
            }
            return;
        }
    }
}

void Column::focus_window(PHLWINDOW window) {
    for (auto win = windows.first(); win != nullptr; win = win->next()) {
        if (win->data()->ptr().lock() == window) {
            active = win;
            return;
        }
    }
}

double Column::get_geom_x() const {
    return geom.x;
}

double Column::get_geom_w() const {
    return geom.w;
}

void Column::set_geom_w(double w) {
    geom.w = w;
}

Vector2D Column::get_height() const {
    Vector2D height;
    auto *first = windows.first()->data();
    auto *last = windows.last()->data();
    height.x = first->get_geom_y();
    height.y = last->get_geom_y() + last->get_geom_h();
    return height;
}

void Column::scale(const Vector2D &bmin, const Vector2D &start, double scale, double gap) {
    for (auto win = windows.first(); win != nullptr; win = win->next()) {
        const auto oldY = win->data()->get_geom_y();
        const auto newY = start.y + (oldY - bmin.y) * scale;
        win->data()->set_geom_y(newY);
        win->data()->set_geom_h(win->data()->get_geom_h() * scale);
        PHLWINDOW window = win->data()->ptr().lock();
        auto border = window->getRealBorderSize();
        auto gap0 = win == windows.first() ? 0.0 : gap;
        window->m_position = Vector2D(start.x + border + geom.x - bmin.x,
                                      win->data()->get_geom_y() + border + gap0);
        auto gap1 = win == windows.last() ? 0.0 : gap;
        window->m_size.x *= scale;
        window->m_size.y = (window->m_size.y + 2.0 * border + gap0 + gap1) * scale - gap0 - gap1 - 2.0 * border;
        window->m_size = Vector2D(std::max(window->m_size.x, 1.0), std::max(window->m_size.y, 1.0));
        sync_window_target_geometry(window);
    }
}

bool Column::toggle_fullscreen(const ScrollerCore::Box &fullbbox, Mode mode) {
    full = fullbbox;

    if (mode == Mode::Row) {
        fullscreened = !fullscreened;
        if (fullscreened) {
            mem.geom = geom;
            geom.w = fullbbox.w;
        } else {
            geom = mem.geom;
        }
        return fullscreened;
    }

    return active->data()->toggle_expand(fullbbox.h);
}

void Column::set_fullscreen(const ScrollerCore::Box &fullbbox) {
    full = fullbbox;
}

void Column::push_geom() {
    mem.geom = geom;
    for (auto w = windows.first(); w != nullptr; w = w->next()) {
        w->data()->push_geom();
    }
}

void Column::pop_geom() {
    geom = mem.geom;
    for (auto w = windows.first(); w != nullptr; w = w->next()) {
        w->data()->pop_geom();
    }
}

void Column::toggle_maximized(double maxw, double maxh) {
    maxdim = !maxdim;
    if (maxdim) {
        mem.geom = geom;
        active->data()->push_geom();
        geom.w = maxw;
        active->data()->set_geom_h(maxh);
    } else {
        geom = mem.geom;
        active->data()->pop_geom();
    }
}

bool Column::fullscreen() const {
    if (!active)
        return false;

    auto window = active->data()->ptr().lock();
    return window ? window->isFullscreen() : false;
}

bool Column::expanded() const {
    return fullscreened || (active && active->data()->expanded());
}

bool Column::maximized() const {
    return maxdim;
}

void Column::set_geom_pos(double x, double y) {
    geom.set_pos(x, y);
}

void Column::recalculate_col_geometry(const Vector2D &gap_x, double gap) {
    if (fullscreen()) {
        PHLWINDOW wactive = active->data()->ptr().lock();
        active->data()->set_geom_y(full.y);
        wactive->m_position = Vector2D(full.x, full.y);
        wactive->m_size = Vector2D(full.w, full.h);
        sync_window_target_geometry(wactive);
        return;
    }

    Window *wactive = active->data();
    PHLWINDOW win = wactive->ptr().lock();
    auto gap0 = active == windows.first() ? 0.0 : gap;
    auto border = win->getRealBorderSize();
    const auto base_x = window_active_x(geom, border, gap_x.x);
    auto a_y0 = std::round(wactive->get_geom_y());
    auto a_y1 = std::round(wactive->get_geom_y() + wactive->get_geom_h());
    const auto top = geom.y;
    const auto bottom = geom.y + geom.h - wactive->get_geom_h();

    spdlog::debug("col_recalc_input: active_window={} logical_y={} logical_h={} geom=({}, {}, {}, {})",
                  static_cast<const void*>(win ? win.get() : nullptr),
                  wactive->get_geom_y(),
                  wactive->get_geom_h(),
                  geom.x,
                  geom.y,
                  geom.w,
                  geom.h);

    if (a_y0 < geom.y) {
        wactive->set_geom_y(top);
        adjust_windows(active, gap_x, gap);
        return;
    }
    if (a_y1 > geom.y + geom.h) {
        wactive->set_geom_y(bottom);
        adjust_windows(active, gap_x, gap);
        return;
    }
    if (reorder != Reorder::Auto) {
        win->m_position.x = base_x;
        adjust_windows(active, gap_x, gap);
        return;
    }

    Window *prev = active->prev() ? active->prev()->data() : nullptr;
    Window *next = active->next() ? active->next()->data() : nullptr;
    const auto prev_gap = active->prev() == windows.first() ? 0.0 : gap;
    const auto next_gap = active->next() == windows.first() ? 0.0 : gap;
    const bool keep_current = is_window_fully_visible(prev, prev_gap, geom) ||
                             is_window_fully_visible(next, next_gap, geom);
    if (keep_current) {
        win->m_position.x = base_x;
        adjust_windows(active, gap_x, gap);
        return;
    }

    const auto next_h = next ? next->get_geom_h() : 0.0;
    const auto prev_h = prev ? prev->get_geom_h() : 0.0;
    const auto active_h = wactive->get_geom_h();
    const double new_y = choose_anchor_y(next != nullptr, prev != nullptr, active_h, next_h, prev_h, geom);
    wactive->set_geom_y(new_y);
    adjust_windows(active, gap_x, gap);
    spdlog::debug("col_recalc_auto: active_window={} keep_current={} prev_visible={} next_visible={} new_y={}",
                  static_cast<const void*>(win ? win.get() : nullptr),
                  keep_current,
                  is_window_fully_visible(prev, prev_gap, geom),
                  is_window_fully_visible(next, next_gap, geom),
                  new_y);
}

PHLWINDOW Column::get_active_window() {
    return active->data()->ptr().lock();
}

void Column::move_active_up() {
    if (active == windows.first())
        return;

    reorder = Reorder::Auto;
    auto prev = active->prev();
    windows.swap(active, prev);
}

void Column::move_active_down() {
    if (active == windows.last())
        return;

    reorder = Reorder::Auto;
    auto next = active->next();
    windows.swap(active, next);
}

FocusMoveResult Column::move_focus_up(bool focus_wrap) {
    if (active != windows.first()) {
        reorder = Reorder::Auto;
        active = active->prev();
        return FocusMoveResult::Moved;
    }

    const auto monitor = g_pCompositor->getMonitorInDirection(Math::fromChar('u'));
    if (monitor != nullptr) {
        return FocusMoveResult::CrossMonitor;
    }
    auto previous = active;
    if (focus_wrap) {
        active = windows.last();
    }
    return active != previous ? FocusMoveResult::Moved : FocusMoveResult::NoOp;
}

FocusMoveResult Column::move_focus_down(bool focus_wrap) {
    if (active != windows.last()) {
        reorder = Reorder::Auto;
        active = active->next();
        return FocusMoveResult::Moved;
    }
    const auto monitor = g_pCompositor->getMonitorInDirection(Math::fromChar('d'));
    if (monitor != nullptr) {
        return FocusMoveResult::CrossMonitor;
    }
    auto previous = active;
    if (focus_wrap) {
        active = windows.first();
    }
    return active != previous ? FocusMoveResult::Moved : FocusMoveResult::NoOp;
}

void Column::admit_window(Window *window) {
    reorder = Reorder::Auto;
    active = windows.emplace_after(active, window);
}

Window *Column::expel_active(double gap) {
    reorder = Reorder::Auto;
    Window *window = active->data();
    auto act = active == windows.first() ? active->next() : active->prev();
    windows.erase(active);
    active = act;
    return window;
}

void Column::align_window(Direction direction, double gap) {
    PHLWINDOW window = active->data()->ptr().lock();
    auto border = window->getRealBorderSize();
    auto gap0 = active == windows.first() ? 0.0 : gap;
    auto gap1 = active == windows.last() ? 0.0 : gap;
    switch (direction) {
    case Direction::Up:
        reorder = Reorder::Lazy;
        active->data()->set_geom_y(geom.y);
        break;
    case Direction::Down:
        reorder = Reorder::Lazy;
        active->data()->set_geom_y(geom.y + geom.h - active->data()->get_geom_h());
        break;
    case Direction::Center:
        reorder = Reorder::Lazy;
        active->data()->set_geom_y(0.5 * (geom.y + geom.h - active->data()->get_geom_h()));
        break;
    default:
        break;
    }
}

ColumnWidth Column::get_width() const {
    return width;
}

void Column::set_width_free() {
    width = ColumnWidth::Free;
}

#ifdef COLORS_IPC
std::string Column::get_width_name() const {
    switch (width) {
    case ColumnWidth::OneThird:
        return "OneThird";
    case ColumnWidth::OneHalf:
        return "OneHalf";
    case ColumnWidth::TwoThirds:
        return "TwoThirds";
    case ColumnWidth::Free:
        return "Free";
    default:
        return "";
    }
}

std::string Column::get_height_name() const {
    switch (height) {
    case WindowHeight::Auto:
        return "Auto";
    case WindowHeight::Free:
        return "Free";
    default:
        return "";
    }
}
#endif

void Column::update_width(ColumnWidth cwidth, double maxw, double maxh) {
    if (maximized()) {
        geom.w = maxw;
    } else {
        switch (cwidth) {
        case ColumnWidth::OneThird:
            geom.w = maxw / 3.0;
            break;
        case ColumnWidth::OneHalf:
            geom.w = maxw / 2.0;
            break;
        case ColumnWidth::TwoThirds:
            geom.w = 2.0 * maxw / 3.0;
            break;
        case ColumnWidth::Free:
            geom.w = maxw;
            break;
        default:
            break;
        }
    }
    geom.h = maxh;
    width = cwidth;
}

void Column::fit_size(FitSize fitsize, const Vector2D &gap_x, double gap) {
    reorder = Reorder::Auto;
    ListNode<Window *> *from, *to;
    switch (fitsize) {
    case FitSize::Active:
        from = to = active;
        break;
    case FitSize::Visible:
        for (auto w = windows.first(); w != nullptr; w = w->next()) {
            auto gap0 = w == windows.first() ? 0.0 : gap;
            Window *win = w->data();
            if (is_window_intersect_viewport(win, gap0, geom)) {
                from = w;
                break;
            }
        }
        for (auto w = windows.last(); w != nullptr; w = w->prev()) {
            auto gap0 = w == windows.first() ? 0.0 : gap;
            Window *win = w->data();
            if (is_window_intersect_viewport(win, gap0, geom)) {
                to = w;
                break;
            }
        }
        break;
    case FitSize::All:
        from = windows.first();
        to = windows.last();
        break;
    case FitSize::ToEnd:
        from = active;
        to = windows.last();
        break;
    case FitSize::ToBeg:
        from = windows.first();
        to = active;
        break;
    default:
        return;
    }

    if (from != nullptr && to != nullptr) {
        double total = 0.0;
        for (auto c = from; c != to->next(); c = c->next()) {
            total += c->data()->get_geom_h();
        }
        for (auto c = from; c != to->next(); c = c->next()) {
            Window *win = c->data();
            win->set_height_free();
            win->set_geom_h(win->get_geom_h() / total * geom.h);
        }
        from->data()->set_geom_y(geom.y);
        adjust_windows(from, gap_x, gap);
    }
}

void Column::cycle_size_active_window(int step, const Vector2D &gap_x, double gap) {
    reorder = Reorder::Auto;
    WindowHeight height = active->data()->get_height();
    if (height == WindowHeight::Free) {
        height = WindowHeight::One;
    } else {
        int number = static_cast<int>(WindowHeight::Number);
        height = static_cast<WindowHeight>((number + static_cast<int>(height) + step) % number);
    }
    active->data()->update_height(height, geom.h);
    recalculate_col_geometry(gap_x, gap);
}

void Column::adjust_windows(ListNode<Window *> *win, const Vector2D &gap_x, double gap) {
    if (win) {
        auto anchorWindow = win->data()->ptr().lock();
        if (anchorWindow) {
            const auto anchorBorder = anchorWindow->getRealBorderSize();
            const auto anchorGap0 = win == windows.first() ? 0.0 : gap;
            anchorWindow->m_position = Vector2D(geom.x + anchorBorder + gap_x.x,
                                                win->data()->get_geom_y() + anchorBorder + anchorGap0);
        }
    }
    for (auto w = win->prev(), p = win; w != nullptr; p = w, w = w->prev()) {
        auto *wdata = w->data();
        auto *pdata = p->data();
        wdata->set_geom_y(pdata->get_geom_y() - wdata->get_geom_h());
        PHLWINDOW ww = w->data()->ptr().lock();
        auto wgap0 = w == windows.first() ? 0.0 : gap;
        auto wborder = ww->getRealBorderSize();
        ww->m_position = Vector2D(geom.x + wborder + gap_x.x, wdata->get_geom_y() + wborder + wgap0);
    }
    for (auto w = win->next(), p = win; w != nullptr; p = w, w = w->next()) {
        auto *wdata = w->data();
        auto *pdata = p->data();
        wdata->set_geom_y(pdata->get_geom_y() + pdata->get_geom_h());
        PHLWINDOW ww = w->data()->ptr().lock();
        auto wborder = ww->getRealBorderSize();
        auto wgap0 = w == windows.first() ? 0.0 : gap;
        ww->m_position = Vector2D(geom.x + wborder + gap_x.x, wdata->get_geom_y() + wborder + wgap0);
    }

    auto anchorWindow = win ? win->data()->ptr().lock() : nullptr;
    auto monitor = anchorWindow ? g_pCompositor->getMonitorFromID(anchorWindow->monitorID()) : nullptr;
    const auto fullTop = monitor ? monitor->m_position.y : geom.y;
    const auto fullBottom = monitor ? monitor->m_position.y + monitor->m_size.y : geom.y + geom.h;
    const auto reservedTop = std::max(0.0, geom.y - fullTop);
    const auto reservedBottom = std::max(0.0, fullBottom - (geom.y + geom.h));

    size_t shiftedAbove = 0;
    size_t shiftedBelow = 0;
    for (auto w = windows.first(); w != nullptr; w = w->next()) {
        auto *wdata = w->data();
        const auto boxTop = wdata->get_geom_y();
        const auto boxBottom = boxTop + wdata->get_geom_h();

        if (reservedTop > 0.0 && boxBottom <= geom.y) {
            wdata->set_geom_y(boxTop - reservedTop);
            shiftedAbove++;
            continue;
        }

        if (reservedBottom > 0.0 && boxTop >= geom.y + geom.h) {
            wdata->set_geom_y(boxTop + reservedBottom);
            shiftedBelow++;
        }
    }

    if (shiftedAbove > 0 || shiftedBelow > 0) {
        spdlog::debug("col_recalc_reserved_shift: active_window={} reserved_top={} reserved_bottom={} shifted_above={} shifted_below={}",
                      static_cast<const void*>(anchorWindow ? anchorWindow.get() : nullptr),
                      reservedTop,
                      reservedBottom,
                      shiftedAbove,
                      shiftedBelow);
    }

    for (auto w = windows.first(); w != nullptr; w = w->next()) {
        PHLWINDOW win = w->data()->ptr().lock();
        auto gap0 = w == windows.first() ? 0.0 : gap;
        auto gap1 = w == windows.last() ? 0.0 : gap;
        auto border = win->getRealBorderSize();
        auto wh = w->data()->get_geom_h();
        win->m_position = Vector2D(geom.x + border + gap_x.x,
                                   w->data()->get_geom_y() + border + gap0);
        win->m_size = Vector2D(std::max(geom.w - 2.0 * border - gap_x.x - gap_x.y, 1.0),
                               std::max(wh - 2.0 * border - gap0 - gap1, 1.0));
        sync_window_target_geometry(win);
    }
}

void Column::resize_active_window(double maxw, const Vector2D &gap_x, double gap, const Vector2D &delta) {
    auto border = active->data()->ptr().lock()->getRealBorderSize();
    auto rwidth = geom.w + delta.x - 2.0 * border - gap_x.x - gap_x.y;
    auto mwidth = geom.w + delta.x - 2.0 * (border + std::max(std::max(gap_x.x, gap_x.y), gap));
    if (mwidth <= 0.0 || rwidth >= maxw)
        return;

    if (std::abs(static_cast<int>(delta.y)) > 0) {
        for (auto win = windows.first(); win != nullptr; win = win->next()) {
            auto gap0 = win == windows.first() ? 0.0 : gap;
            auto gap1 = win == windows.last() ? 0.0 : gap;
            auto wh = win->data()->get_geom_h() - gap0 - gap1 - 2.0 * border;
            if (win == active)
                wh += delta.y;
            if (wh <= 0.0 || wh + 2.0 * win->data()->ptr().lock()->getRealBorderSize() + gap0 + gap1 > geom.h)
                return;
        }
    }
    reorder = Reorder::Auto;
    width = ColumnWidth::Free;

    geom.w += delta.x;
    if (std::abs(static_cast<int>(delta.y)) > 0) {
        for (auto win = windows.first(); win != nullptr; win = win->next()) {
            Window *window = win->data();
            if (win == active)
                window->set_geom_h(window->get_geom_h() + delta.y);
        }
    }
}

} // namespace ScrollerModel
