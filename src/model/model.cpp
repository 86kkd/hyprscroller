#include "model.h"

#include <algorithm>
#include <cmath>

#include <hyprlang.hpp>
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
// Return true when a neighbor window is fully inside the current geometry after
// border and gap offsets are applied.
static bool is_window_fully_visible(Window *window, double gap, const ScrollerCore::Box &geom) {
    if (!window)
        return false;
    auto phWindow = window->ptr().lock();
    const auto border = phWindow->getRealBorderSize();
    const auto y0 = std::round(phWindow->m_position.y - border - gap);
    const auto y1 = std::round(phWindow->m_position.y - border - gap + window->get_geom_h());
    return y0 >= geom.y && y1 <= geom.y + geom.h;
}

// Return true when a window intersects the geometry by any overlap.
// Shared logic extracted from the previous inline visibility checks.
static bool is_window_intersect_viewport(Window *window, double gap, const ScrollerCore::Box &geom) {
    if (!window)
        return false;
    const auto phWindow = window->ptr().lock();
    const auto border = phWindow->getRealBorderSize();
    const auto y0 = phWindow->m_position.y - border;
    const auto y1 = phWindow->m_position.y - border - gap + window->get_geom_h();
    return y0 < geom.y + geom.h && y0 >= geom.y ||
           y1 > geom.y && y1 <= geom.y + geom.h ||
           y0 < geom.y && y1 >= geom.y + geom.h;
}

static double window_active_x(const ScrollerCore::Box &geom, double border_x, double gap_x) {
    return geom.x + border_x + gap_x;
}

// Choose anchor y for non-default layouts:
// - if both neighbors exist, prefer stacking against bottom/next when possible;
// - fall back to placing against top when no safe anchored slot exists.
static double choose_anchor_y(bool has_next, bool has_prev, double active_h, double next_h, double prev_h,
                             const ScrollerCore::Box &geom, double border, double gap0) {
    const auto base_y = geom.y + border + gap0;
    const auto stack_to_bottom = geom.y + geom.h - active_h + border + gap0;
    if (has_next && active_h + next_h <= geom.h) {
        return geom.y + geom.h - active_h - next_h + border + gap0;
    }
    if (has_next && has_prev && prev_h + active_h <= geom.h) {
        return geom.y + prev_h + border + gap0;
    }
    if (!has_next && has_prev && prev_h + active_h <= geom.h) {
        return geom.y + prev_h + border + gap0;
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

// Internal window metadata wrapper used by a column.
Window::Window(PHLWINDOW window, double box_h) : window(window), height(WindowHeight::One), box_h(box_h) {}

PHLWINDOWREF Window::ptr() {
    return window;
}

double Window::get_geom_h() const {
    return box_h;
}

void Window::set_geom_h(double geom_h) {
    box_h = geom_h;
}

void Window::push_geom() {
    // Snapshot logical height and native compositor y-position before temporary edits.
    mem.box_h = box_h;
    mem.pos_y = window.lock()->m_position.y;
}

void Window::pop_geom() {
    // Roll back to the previously snapshotted values.
    box_h = mem.box_h;
    window.lock()->m_position.y = mem.pos_y;
}

bool Window::toggle_expand(double maxh) {
    if (is_expanded) {
        pop_geom();
        is_expanded = false;
        return false;
    }

    push_geom();
    box_h = maxh;
    is_expanded = true;
    return true;
}

bool Window::expanded() const {
    return is_expanded;
}

WindowHeight Window::get_height() const {
    return height;
}

void Window::update_height(WindowHeight h, double max) {
    height = h;
    switch (height) {
    case WindowHeight::One:
        box_h = max;
        break;
    case WindowHeight::TwoThirds:
        box_h = 2.0 * max / 3.0;
        break;
    case WindowHeight::OneHalf:
        box_h = 0.5 * max;
        break;
    case WindowHeight::OneThird:
        box_h = max / 3.0;
        break;
    default:
        break;
    }
}

void Window::set_height_free() {
    height = WindowHeight::Free;
}

// A column is a vertical list of one or more windows sharing horizontal bounds.
Column::Column(PHLWINDOW cwindow, double maxw, double maxh)
    : height(WindowHeight::One), reorder(Reorder::Auto), initialized(false), maxdim(false) {
    // Resolve width defaults from config, with floating-window width fallback.
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
    // Reused constructor: caller controls width and geometry baselines.
    window->set_geom_h(maxh);
    update_width(width, maxw, maxh);
    geom.h = maxh;
    windows.push_back(window);
    active = windows.first();
}

Column::~Column() {
    // Column owns Window instances; delete them on destruction.
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

    // Resolve both matching nodes first, then swap only active indices and list order.
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
    // Insert right after active node so focus remains near last interaction.
    const auto previous = active;
    const auto new_height = previous ? previous->data()->get_geom_h() : maxh;
    active = windows.emplace_after(active, new Window(window, new_height));

    if (!previous)
        return;

    auto new_window = active->data()->ptr().lock();
    auto previous_window = previous->data()->ptr().lock();
    if (!new_window || !previous_window)
        return;

    new_window->m_position = Vector2D(previous_window->m_position.x,
                                      previous_window->m_position.y + previous->data()->get_geom_h());
}

void Column::remove_window(PHLWINDOW window) {
    reorder = Reorder::Auto;
    // Keep active pointer deterministic after erase by preferring next, then prev.
    for (auto win = windows.first(); win != nullptr; win = win->next()) {
        if (win->data()->ptr().lock() == window) {
            if (window == active->data()->ptr().lock()) {
                // Make next window active (like PaperWM).
                // If it is the last, make the previous one active.
                // If it is the only window, active will point to nullptr,
                // and callers will remove the parent column.
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
    // Move active pointer to the list node owned by this native window.
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
    PHLWINDOW first = windows.first()->data()->ptr().lock();
    PHLWINDOW last = windows.last()->data()->ptr().lock();
    height.x = first->m_position.y - first->getRealBorderSize();
    height.y = last->m_position.y + last->m_size.y + last->getRealBorderSize();
    return height;
}

void Column::scale(const Vector2D &bmin, const Vector2D &start, double scale, double gap) {
    // Rescale logical model heights then propagate to native compositor geometry.
    for (auto win = windows.first(); win != nullptr; win = win->next()) {
        win->data()->set_geom_h(win->data()->get_geom_h() * scale);
        PHLWINDOW window = win->data()->ptr().lock();
        auto border = window->getRealBorderSize();
        auto gap0 = win == windows.first() ? 0.0 : gap;
        window->m_position = start + Vector2D(border, border) + (window->m_position - Vector2D(border, border) - bmin) * scale;
        window->m_position.y += gap0;
        auto gap1 = win == windows.last() ? 0.0 : gap;
        window->m_size.x *= scale;
        window->m_size.y = (window->m_size.y + 2.0 * border + gap0 + gap1) * scale - gap0 - gap1 - 2.0 * border;
        window->m_size = Vector2D(std::max(window->m_size.x, 1.0), std::max(window->m_size.y, 1.0));
        sync_window_target_geometry(window);
    }
}

bool Column::toggle_fullscreen(const ScrollerCore::Box &fullbbox, Mode mode) {
    // Plugin fullscreen only expands along the layout axis.
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
    // Save current column and child geometry before non-linear transform operations.
    mem.geom = geom;
    for (auto w = windows.first(); w != nullptr; w = w->next()) {
        w->data()->push_geom();
    }
}

void Column::pop_geom() {
    // Restore column and child geometry from previous push.
    geom = mem.geom;
    for (auto w = windows.first(); w != nullptr; w = w->next()) {
        w->data()->pop_geom();
    }
}

void Column::toggle_maximized(double maxw, double maxh) {
    // Enter/exit maxdim; keep previous state in memory for exact rollback.
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
    // Keep fullscreen path authoritative, then recompute non-fullscreen bounds.
    if (fullscreen()) {
        PHLWINDOW wactive = active->data()->ptr().lock();
        wactive->m_position = Vector2D(full.x, full.y);
        wactive->m_size = Vector2D(full.w, full.h);
        sync_window_target_geometry(wactive);
        return;
    }

    // In theory, every window in the column should have the same size.
    // Windows near monitor edges can differ due to inner/outer gap rules.
    Window *wactive = active->data();
    PHLWINDOW win = wactive->ptr().lock();
    auto gap0 = active == windows.first() ? 0.0 : gap;
    auto border = win->getRealBorderSize();
    const auto base_x = window_active_x(geom, border, gap_x.x);
    auto a_y0 = std::round(win->m_position.y - border - gap0);
    auto a_y1 = std::round(win->m_position.y - border - gap0 + wactive->get_geom_h());
    const auto top = geom.y + border + gap0;
    const auto bottom = geom.y + geom.h - wactive->get_geom_h() + border + gap0;

    if (a_y0 < geom.y) {
        // active starts above: clamp to top edge
        win->m_position = Vector2D(base_x, top);
        adjust_windows(active, gap_x, gap);
        return;
    }
    if (a_y1 > geom.y + geom.h) {
        // active overflows bottom: clamp to bottom edge
        win->m_position = Vector2D(base_x, bottom);
        adjust_windows(active, gap_x, gap);
        return;
    }
    if (reorder != Reorder::Auto) {
        // In lazy mode we keep y position and only normalize x.
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
    const double new_y = choose_anchor_y(next != nullptr, prev != nullptr, active_h, next_h, prev_h, geom, border, gap0);
    win->m_position = Vector2D(base_x, new_y);
    adjust_windows(active, gap_x, gap);
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

bool Column::move_focus_up(bool focus_wrap) {
    // If inside list, move active upward locally.
    if (active != windows.first()) {
        reorder = Reorder::Auto;
        active = active->prev();
        return true;
    }

    const auto monitor = g_pCompositor->getMonitorInDirection(Math::fromChar('u'));
    if (monitor != nullptr) {
        g_pKeybindManager->m_dispatchers["movefocus"]("u");
        return false;
    }
    if (focus_wrap) {
        active = windows.last();
    }
    return true;
}

bool Column::move_focus_down(bool focus_wrap) {
    // If inside list, move active downward locally.
    if (active != windows.last()) {
        reorder = Reorder::Auto;
        active = active->next();
        return true;
    }
    const auto monitor = g_pCompositor->getMonitorInDirection(Math::fromChar('d'));
    if (monitor != nullptr) {
        g_pKeybindManager->m_dispatchers["movefocus"]("d");
        return false;
    }
    if (focus_wrap) {
        active = windows.first();
    }
    return true;
}

void Column::admit_window(Window *window) {
    // Insert a foreign window node after current active.
    reorder = Reorder::Auto;
    active = windows.emplace_after(active, window);
}

Window *Column::expel_active(double gap) {
    // Remove active node and return it to caller; keep cursor valid around neighbors.
    reorder = Reorder::Auto;
    Window *window = active->data();
    auto act = active == windows.first() ? active->next() : active->prev();
    windows.erase(active);
    active = act;
    return window;
}

void Column::align_window(Direction direction, double gap) {
    // Align active window to top/center/bottom inside current column geometry.
    PHLWINDOW window = active->data()->ptr().lock();
    auto border = window->getRealBorderSize();
    auto gap0 = active == windows.first() ? 0.0 : gap;
    auto gap1 = active == windows.last() ? 0.0 : gap;
    switch (direction) {
    case Direction::Up:
        reorder = Reorder::Lazy;
        window->m_position.y = geom.y + border + gap0;
        break;
    case Direction::Down:
        reorder = Reorder::Lazy;
        window->m_position.y = geom.y + geom.h - border + gap1;
        break;
    case Direction::Center:
        reorder = Reorder::Lazy;
        window->m_position.y = 0.5 * (geom.y + geom.h - (2.0 * border + gap0 + gap1 + window->m_size.y));
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
    // Apply preset width mapping; width is ignored when already maximized.
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
            // Only used when creating a column from an expelled window.
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
    // Recompute a contiguous range and redistribute height so selected windows fill workspace.
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
        auto c = from;
        double total = 0.0;
        for (auto c = from; c != to->next(); c = c->next()) {
            total += c->data()->get_geom_h();
        }
        for (auto c = from; c != to->next(); c = c->next()) {
            Window *win = c->data();
            win->set_height_free();
            win->set_geom_h(win->get_geom_h() / total * geom.h);
        }
        auto gap0 = from == windows.first() ? 0.0 : gap;
        PHLWINDOW from_window = from->data()->ptr().lock();
        from_window->m_position.y = geom.y + gap0 + from_window->getRealBorderSize();

        adjust_windows(from, gap_x, gap);
    }
}

void Column::cycle_size_active_window(int step, const Vector2D &gap_x, double gap) {
    reorder = Reorder::Auto;
    WindowHeight height = active->data()->get_height();
    if (height == WindowHeight::Free) {
        // When cycle-resizing from Free mode, reset to full size state first.
        height = WindowHeight::One;
    } else {
        int number = static_cast<int>(WindowHeight::Number);
        height = static_cast<WindowHeight>((number + static_cast<int>(height) + step) % number);
    }
    active->data()->update_height(height, geom.h);
    recalculate_col_geometry(gap_x, gap);
}

void Column::adjust_windows(ListNode<Window *> *win, const Vector2D &gap_x, double gap) {
    // Walk outward from anchor node and rebuild sibling positions and sizes.
    for (auto w = win->prev(), p = win; w != nullptr; p = w, w = w->prev()) {
        PHLWINDOW ww = w->data()->ptr().lock();
        PHLWINDOW pw = p->data()->ptr().lock();
        auto wgap0 = w == windows.first() ? 0.0 : gap;
        auto wborder = ww->getRealBorderSize();
        auto pborder = pw->getRealBorderSize();
        ww->m_position = Vector2D(geom.x + wborder + gap_x.x, pw->m_position.y - gap - pborder - w->data()->get_geom_h() + wborder + wgap0);
    }
    for (auto w = win->next(), p = win; w != nullptr; p = w, w = w->next()) {
        PHLWINDOW ww = w->data()->ptr().lock();
        PHLWINDOW pw = p->data()->ptr().lock();
        auto pgap0 = p == windows.first() ? 0.0 : gap;
        auto wborder = ww->getRealBorderSize();
        auto pborder = pw->getRealBorderSize();
        ww->m_position = Vector2D(geom.x + wborder + gap_x.x, pw->m_position.y - pborder - pgap0 + p->data()->get_geom_h() + wborder + gap);
    }
    for (auto w = windows.first(); w != nullptr; w = w->next()) {
        PHLWINDOW win = w->data()->ptr().lock();
        auto gap0 = w == windows.first() ? 0.0 : gap;
        auto gap1 = w == windows.last() ? 0.0 : gap;
        auto border = win->getRealBorderSize();
        auto wh = w->data()->get_geom_h();
        win->m_size = Vector2D(std::max(geom.w - 2.0 * border - gap_x.x - gap_x.y, 1.0),
                               std::max(wh - 2.0 * border - gap0 - gap1, 1.0));
        sync_window_target_geometry(win);
    }
}

void Column::resize_active_window(double maxw, const Vector2D &gap_x, double gap, const Vector2D &delta) {
    // Validate resize first; abort if any resulting geometry would be invalid.
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
