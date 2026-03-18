//#define COLORS_IPC

#include <hyprland/src/desktop/view/Window.hpp>
//#include <hyprland/src/Window.hpp>
#include <hyprland/src/config/ConfigManager.hpp>
#include <hyprland/src/config/ConfigValue.hpp>
#include <hyprland/src/plugins/PluginAPI.hpp>
#include <hyprutils/math/Vector2D.hpp>
#include <hyprland/src/desktop/Workspace.hpp>
#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/managers/KeybindManager.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprland/src/layout/space/Space.hpp>
#include <hyprland/src/helpers/Monitor.hpp>
#ifdef COLORS_IPC
#include <hyprland/src/managers/EventManager.hpp>
#endif

#include "core/core.h"
#include "model/model.h"
#include "scroller.h"

extern HANDLE PHANDLE;
using namespace ScrollerCore;
using namespace ScrollerModel;

#if 0

enum class ColumnWidth {
    OneThird = 0,
    OneHalf,
    TwoThirds,
    Number,
    Free
};

enum class WindowHeight {
    OneThird,
    OneHalf,
    TwoThirds,
    One,
    Number,
    Free
};

enum class Reorder {
    Auto,
    Lazy
};

// Internal window wrapper used by Column to keep geometry, history, and height mode.
class Window {
public:
    Window(PHLWINDOW window, double box_h) : window(window), height(WindowHeight::One), box_h(box_h) {}
    PHLWINDOWREF ptr() { return window; }
    double get_geom_h() const { return box_h; }
    void set_geom_h(double geom_h) { box_h = geom_h; }
    void push_geom() {
        mem.box_h = box_h;
        mem.pos_y = window.lock()->m_position.y;
    }
    void pop_geom() {
        box_h = mem.box_h;
        window.lock()->m_position.y = mem.pos_y;
    }
    WindowHeight get_height() const { return height; }
    void update_height(WindowHeight h, double max) {
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
    void set_height_free() { height = WindowHeight::Free; }
private:
    struct Memory {
        double pos_y;
        double box_h;
    };
    PHLWINDOWREF window;
    WindowHeight height;
    double box_h;
    Memory mem;    // memory to store old height and win y when in maximized/overview modes
};

class Column {
    // A column is a vertical list of one or more windows sharing horizontal bounds.
public:
    Column(PHLWINDOW cwindow, double maxw, double maxh)
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
    Column(Window *window, ColumnWidth width, double maxw, double maxh)
        : width(width), height(WindowHeight::One), reorder(Reorder::Auto), initialized(true), maxdim(false) {
        window->set_geom_h(maxh);
        update_width(width, maxw, maxh);
        geom.h = maxh;
        windows.push_back(window);
        active = windows.first();
    }
    ~Column() {
        for (auto win = windows.first(); win != nullptr; win = win->next()) {
            delete win->data();
        }
        windows.clear();
    }
    bool get_init() const { return initialized; }
    void set_init() { initialized = true; }
    size_t size() {
        return windows.size();
    }
    bool has_window(PHLWINDOW window) const {
        for (auto win = windows.first(); win != nullptr; win = win->next()) {
            if (win->data()->ptr().lock() == window)
                return true;
        }
        return false;
    }
    bool swap_windows(PHLWINDOW a, PHLWINDOW b) {
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
    void add_active_window(PHLWINDOW window, double maxh) {
        reorder = Reorder::Auto;
        active = windows.emplace_after(active, new Window(window, maxh));
    }
    void remove_window(PHLWINDOW window) {
        reorder = Reorder::Auto;
        for (auto win = windows.first(); win != nullptr; win = win->next()) {
            if (win->data()->ptr().lock() == window) {
                if (window == active->data()->ptr().lock()) {
                    // Make next window active (like PaperWM)
                    // If it is the last, make the previous one active.
                    // If it is the only window. active will point to nullptr,
                    // but it doesn't matter because the caller will delete
                    // the column.
                    active = active != windows.last() ? active->next() : active->prev();
                }
                windows.erase(win);
                return;
            }
        }
    }
    void focus_window(PHLWINDOW window) {
        for (auto win = windows.first(); win != nullptr; win = win->next()) {
            if (win ->data()->ptr().lock() == window) {
                active = win;
                return;
            }
        }
    }
    double get_geom_x() const {
        return geom.x;
    }
    double get_geom_w() const {
        return geom.w;
    }
    // Used by Row::fit_width()
    void set_geom_w(double w) {
        geom.w = w;
    }
    Vector2D get_height() const {
        Vector2D height;
        PHLWINDOW first = windows.first()->data()->ptr().lock();
        PHLWINDOW last = windows.last()->data()->ptr().lock();
        height.x = first->m_position.y - first->getRealBorderSize();
        height.y = last->m_position.y + last->m_size.y + last->getRealBorderSize();
        return height;
    }
    void scale(const Vector2D &bmin, const Vector2D &start, double scale, double gap) {
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
            if (window->m_realSize) {
                *window->m_realSize = window->m_size;
                *window->m_realPosition = window->m_position;
            }
        }
    }
    bool toggle_fullscreen(const Box &fullbbox) {
        PHLWINDOW wactive = active->data()->ptr().lock();
        const bool will_fullscreen = !wactive->isFullscreen();
        if (const auto target = wactive->layoutTarget(); target) {
            target->setFullscreenMode(will_fullscreen ? FSMODE_FULLSCREEN : FSMODE_NONE);
        }
        if (will_fullscreen) {
            full = fullbbox;
        }
        return will_fullscreen;
    }
    // Sets fullscreen even if the active window is not full screen
    // Used in recalculateMonitor
    void set_fullscreen(const Box &fullbbox) {
        // Leave it like this (without enabling full screen in the window).
        // If this is called, it won't work unless the window is also set to full screen
        full = fullbbox;
    }
    void push_geom() {
        mem.geom = geom;
        for (auto w = windows.first(); w != nullptr; w = w->next()) {
            w->data()->push_geom();
        }
    }
    void pop_geom() {
        geom = mem.geom;
        for (auto w = windows.first(); w != nullptr; w = w->next()) {
            w->data()->pop_geom();
        }
    }
    void toggle_maximized(double maxw, double maxh) {
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
    bool fullscreen() const {
        if (!active)
            return false;

        auto window = active->data()->ptr().lock();
        return window ? window->isFullscreen() : false;
    }
    bool maximized() const {
        return maxdim;
    }
    // Used by auto-centering of columns
    void set_geom_pos(double x, double y) {
        geom.set_pos(x, y);
    }
    // Recalculates the geometry of the windows in the column
    void recalculate_col_geometry(const Vector2D &gap_x, double gap) {
        if (fullscreen()) {
            PHLWINDOW wactive = active->data()->ptr().lock();
            wactive->m_position = Vector2D(full.x, full.y);
            wactive->m_size = Vector2D(full.w, full.h);
            if (wactive->m_realPosition) {
                *wactive->m_realPosition = wactive->m_position;
                *wactive->m_realSize = wactive->m_size;
            }
        } else {
            // In theory, every window in the Columm should have the same size,
            // but the standard layouts don't follow this rule (to make the code
            // simpler?). Windows close to the border of the monitor will have
            // their sizes affected by gaps_out vs. gaps_in.
            // I follow the same rules.
            // Each window has a gap to its bounding box of "gaps_in + border",
            // except on the monitor sides, where the gap is "gaps_out + border",
            // but the window sizes are different because of those different
            // gaps. So the distance between two window border boundaries is
            // two times gaps_in (one per window).
            Window *wactive = active->data();
            PHLWINDOW win = wactive->ptr().lock();
            auto gap0 = active == windows.first() ? 0.0 : gap;
            auto gap1 = active == windows.last() ? 0.0 : gap;
            auto border = win->getRealBorderSize();
            auto a_y0 = std::round(win->m_position.y - border - gap0);
            auto a_y1 = std::round(win->m_position.y - border - gap0 + wactive->get_geom_h());
            if (a_y0 < geom.y) {
                // active starts above, set it on the top edge
                win->m_position = Vector2D(geom.x + border + gap_x.x, geom.y + border + gap0);
            } else if (a_y1 > geom.y + geom.h) {
                // active overflows below the bottom, move to bottom of viewport
                win->m_position = Vector2D(geom.x + border + gap_x.x, geom.y + geom.h - wactive->get_geom_h() + border + gap0);
            } else {
                // active window is inside the viewport
                if (reorder == Reorder::Auto) {
                    // The active window should always be completely in the viewport.
                    // If any of the windows next to it, above or below are already
                    // in the viewport, keep the current position.
                    bool keep_current = false;
                    if (active->prev() != nullptr) {
                        Window *prev = active->prev()->data();
                        PHLWINDOW prev_window = prev->ptr().lock();
                        auto gap0 = active->prev() == windows.first() ? 0.0 : gap;
                        auto border = prev_window->getRealBorderSize();
                        auto p_y0 = std::round(prev_window->m_position.y - border - gap0);
                        auto p_y1 = std::round(prev_window->m_position.y - border - gap0 + prev->get_geom_h());
                        if (p_y0 >= geom.y && p_y1 <= geom.y + geom.h) {
                            keep_current = true;
                        }
                    }
                    if (!keep_current && active->next() != nullptr) {
                        Window *next = active->next()->data();
                        PHLWINDOW next_window = next->ptr().lock();
                        auto gap0 = active->next() == windows.first() ? 0.0 : gap;
                        auto border = next_window->getRealBorderSize();
                        auto p_y0 = std::round(next_window->m_position.y - border - gap0);
                        auto p_y1 = std::round(next_window->m_position.y - border - gap0 + next->get_geom_h());
                        if (p_y0 >= geom.y && p_y1 <= geom.y + geom.h) {
                            keep_current = true;
                        }
                    }
                    if (!keep_current) {
                        // If not:
                        // We try to fit the window right below it if it fits
                        // completely, otherwise the one above it. If none of them fit,
                        // we leave it as it is.
                        if (active->next() != nullptr) {
                            if (wactive->get_geom_h() + active->next()->data()->get_geom_h() <= geom.h) {
                                // set next at the bottom edge of the viewport
                                win->m_position = Vector2D(geom.x + border + gap_x.x, geom.y + geom.h - wactive->get_geom_h() - active->next()->data()->get_geom_h() + border + gap0);
                            } else if (active->prev() != nullptr) {
                                if (active->prev()->data()->get_geom_h() + wactive->get_geom_h() <= geom.h) {
                                    // set previous at the top edge of the viewport
                                    win->m_position = Vector2D(geom.x + border + gap_x.x, geom.y + active->prev()->data()->get_geom_h() + border + gap0);
                                } else {
                                    // none of them fit, leave active as it is (only modify x)
                                    win->m_position.x = geom.x + border + gap_x.x;
                                }
                            } else {
                                // nothing above, move active to top of viewport
                                win->m_position = Vector2D(geom.x + border + gap_x.x, geom.y + border + gap0);
                            }
                        } else if (active->prev() != nullptr) {
                            if (active->prev()->data()->get_geom_h() + wactive->get_geom_h() <= geom.h) {
                                // set previous at the top edge of the viewport
                                win->m_position = Vector2D(geom.x + border + gap_x.x, geom.y + active->prev()->data()->get_geom_h() + border + gap0);
                            } else {
                                // it doesn't fit and nothing above, move active to bottom of viewport
                                win->m_position = Vector2D(geom.x + border + gap_x.x, geom.y + geom.h - wactive->get_geom_h() + border + gap0);
                            }
                        } else {
                            // nothing on the right or left, the window is in a correct position
                            win->m_position.x = geom.x + border + gap_x.x;
                        }
                    } else {
                        // the window is in a correct position
                        win->m_position.x = geom.x + border + gap_x.x;
                    }
                } else {
                    // the window is in a correct position
                    win->m_position.x = geom.x + border + gap_x.x;
                }
            }
            adjust_windows(active, gap_x, gap);
        }
    }
    PHLWINDOW get_active_window() {
        return active->data()->ptr().lock();
    }
    void move_active_up() {
        if (active == windows.first())
            return;

        reorder = Reorder::Auto;
        auto prev = active->prev();
        windows.swap(active, prev);
    }
    void move_active_down() {
        if (active == windows.last())
            return;

        reorder = Reorder::Auto;
        auto next = active->next();
        windows.swap(active, next);
    }
    bool move_focus_up(bool focus_wrap) {
        if (active == windows.first()) {
            PHLMONITOR monitor = g_pCompositor->getMonitorInDirection(Math::fromChar('u'));
            if (monitor == nullptr) {
                if (focus_wrap)
                    active = windows.last();
                return true;
            }
            // use default dispatch for movefocus (change monitor)
            g_pKeybindManager->m_dispatchers["movefocus"]("u");
            return false;
        }
        reorder = Reorder::Auto;
        active = active->prev();
        return true;
    }
    bool move_focus_down(bool focus_wrap) {
        if (active == windows.last()) {
            PHLMONITOR monitor = g_pCompositor->getMonitorInDirection(Math::fromChar('d'));
            if (monitor == nullptr) {
                if (focus_wrap)
                    active = windows.first();
                return true;
            }
            // use default dispatch for movefocus (change monitor)
            g_pKeybindManager->m_dispatchers["movefocus"]("d");
            return false;
        }
        reorder = Reorder::Auto;
        active = active->next();
        return true;
    }
    void admit_window(Window *window) {
        reorder = Reorder::Auto;
        active = windows.emplace_after(active, window);
    }

    Window *expel_active(double gap) {
        reorder = Reorder::Auto;
        Window *window = active->data();
        auto act = active == windows.first() ? active->next() : active->prev();
        windows.erase(active);
        active = act;
        return window;
    }
    void align_window(Direction direction, double gap) {
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
    ColumnWidth get_width() const {
        return width;
    }
    // used by Row::fit_width()
    void set_width_free() {
        width = ColumnWidth::Free;
    }
#ifdef COLORS_IPC
    // For IPC events
    std::string get_width_name() const {
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
    std::string get_height_name() const {
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
    void update_width(ColumnWidth cwidth, double maxw, double maxh) {
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
                // Only used when creating a column from an expelled window
                geom.w = maxw;
            default:
                break;
            }
        }
        geom.h = maxh;
        width = cwidth;
    }
    void fit_size(FitSize fitsize, const Vector2D &gap_x, double gap) {
        reorder = Reorder::Auto;
        ListNode<Window *> *from, *to;
        switch (fitsize) {
        case FitSize::Active:
            from = to = active;
            break;
        case FitSize::Visible:
            for (auto w = windows.first(); w != nullptr; w = w->next()) {
                auto gap0 = w == windows.first() ? 0.0 : gap;
                auto gap1 = w == windows.last() ? 0.0 : gap;
                Window *win = w->data();
                PHLWINDOW window = win->ptr().lock();
                auto border = window->getRealBorderSize();
                auto c0 = window->m_position.y - border;
                auto c1 = window->m_position.y - border - gap0 + win->get_geom_h();
                if (c0 < geom.y + geom.h && c0 >= geom.y ||
                    c1 > geom.y && c1 <= geom.y + geom.h ||
                    //should never happen as windows are never taller than the screen
                    c0 < geom.y && c1 >= geom.y + geom.h) {
                    from = w;
                    break;
                }
            }
            for (auto w = windows.last(); w != nullptr; w = w->prev()) {
                auto gap0 = w == windows.first() ? 0.0 : gap;
                auto gap1 = w == windows.last() ? 0.0 : gap;
                Window *win = w->data();
                PHLWINDOW window = win->ptr().lock();
                auto border = window->getRealBorderSize();
                auto c0 = window->m_position.y - border;
                auto c1 = window->m_position.y - border - gap0 + win->get_geom_h();
                if (c0 < geom.y + geom.h && c0 >= geom.y ||
                    c1 > geom.y && c1 <= geom.y + geom.h ||
                    //should never happen as columns are never wider than the screen
                    c0 < geom.y && c1 >= geom.y + geom.h) {
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

        // Now align from top of the screen (geom.y), split height of
        // screen (geom.h) among from->to, and readapt the rest
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
    void cycle_size_active_window(int step, const Vector2D &gap_x, double gap) {
        reorder = Reorder::Auto;
        WindowHeight height = active->data()->get_height();
        if (height == WindowHeight::Free) {
            // When cycle-resizing from Free mode, always move back to One
            height = WindowHeight::One;
        } else {
            int number = static_cast<int>(WindowHeight::Number);
            height =static_cast<WindowHeight>(
                    (number + static_cast<int>(height) + step) % number);
        }
        active->data()->update_height(height, geom.h);
        recalculate_col_geometry(gap_x, gap);
    }
private:
    // Adjust all the windows in the column using 'window' as anchor
    void adjust_windows(ListNode<Window *> *win, const Vector2D &gap_x, double gap) {
        // 2. adjust positions of windows above
        for (auto w = win->prev(), p = win; w != nullptr; p = w, w = w->prev()) {
            PHLWINDOW ww = w->data()->ptr().lock();
            PHLWINDOW pw = p->data()->ptr().lock();
            auto wgap0 = w == windows.first() ? 0.0 : gap;
            auto wborder = ww->getRealBorderSize();
            auto pborder = pw->getRealBorderSize();
            ww->m_position = Vector2D(geom.x + wborder + gap_x.x, pw->m_position.y - gap - pborder - w->data()->get_geom_h() + wborder + wgap0);
        }
        // 3. adjust positions of windows below
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
            //win->m_vSize = Vector2D(geom.w - 2.0 * border - gap_x.x - gap_x.y, wh - 2.0 * border - gap0 - gap1);
            win->m_size = Vector2D(std::max(geom.w - 2.0 * border - gap_x.x - gap_x.y, 1.0), std::max(wh - 2.0 * border - gap0 - gap1, 1.0));
            if (win->m_realPosition) {
                *win->m_realPosition = win->m_position;
                *win->m_realSize = win->m_size;
            }
        }
    }
public:
    void resize_active_window(double maxw, const Vector2D &gap_x, double gap, const Vector2D &delta) {
        // First, check if resize is possible or it would leave any window
        // with an invalid size.

        // Width check
        auto border = active->data()->ptr().lock()->getRealBorderSize();
        auto rwidth = geom.w + delta.x - 2.0 * border - gap_x.x - gap_x.y;
        // Now we check for a size smaller than the maximum possible gap, so
        // we never get in trouble when a window gets expelled from a column
        // with gaps_out, gaps_in, to a column with gaps_in on both sides.
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
                    // geom.h already includes gaps_out
                    return;
            }
        }
        reorder = Reorder::Auto;
        // Now, resize.
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

private:
    struct Memory {
        Box geom;
    };
    ColumnWidth width;
    WindowHeight height;
    Reorder reorder;
    bool initialized;
    Box geom;        // bbox of column
    bool maxdim;     // maximized?
    Memory mem;      // memory of the column's box while in maximized/overview mode
    Box full;        // full screen geometry
    ListNode<Window *> *active;
    List<Window *> windows;
};
#endif

// Global mark storage lives in core module and is shared by dispatcher helpers.
static Marks marks;

class Row {
    // A row contains all columns for one workspace and owns horizontal navigation.
public:
    Row(PHLWINDOW window)
        : workspace(window->workspaceID()), mode(Mode::Row), reorder(Reorder::Auto),
        overview(false), active(nullptr) {
        const auto monitor = g_pCompositor->getMonitorFromID(window->monitorID());
        if (!monitor)
            return;
        update_sizes(monitor);
    }
    ~Row() {
        for (auto col = columns.first(); col != nullptr; col = col->next()) {
            delete col->data();
        }
        columns.clear();
    }
    int get_workspace() const { return workspace; }
    bool has_window(PHLWINDOW window) const {
        for (auto col = columns.first(); col != nullptr; col = col->next()) {
            if (col->data()->has_window(window))
                return true;
        }
        return false;
    }
    PHLWINDOW get_active_window() const {
        return active->data()->get_active_window();
    }
    bool is_active(PHLWINDOW window) const {
        return get_active_window() == window;
    }
    void add_active_window(PHLWINDOW window) {
        if (mode == Mode::Column) {
            active->data()->add_active_window(window, max.h);
            active->data()->recalculate_col_geometry(calculate_gap_x(active), gap);
            return;
        }
        active = columns.emplace_after(active, new Column(window, max.w, max.h));
        reorder = Reorder::Auto;
        recalculate_row_geometry();
    }

    // Remove a window and re-adapt rows and columns, returning
    // true if successful, or false if this is the last row
    // so the layout can remove it.
    bool remove_window(PHLWINDOW window) {
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
                    c->data()->recalculate_col_geometry(calculate_gap_x(c), gap);
                    return true;
                }
            }
        }
        return true;
    }
    bool swapWindows(PHLWINDOW a, PHLWINDOW b) {
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
    void focus_window(PHLWINDOW window) {
        for (auto c = columns.first(); c != nullptr; c = c->next()) {
            if (c->data()->has_window(window)) {
                c->data()->focus_window(window);
                active = c;
                recalculate_row_geometry();
                return;
            }
        }
    }
    bool move_focus(Direction dir, bool focus_wrap) {
        reorder = Reorder::Auto;
        switch (dir) {
        case Direction::Left:
            if (!move_focus_left(focus_wrap))
                return false;
            break;
        case Direction::Right:
            if (!move_focus_right(focus_wrap))
                return false;
            break;
        case Direction::Up:
            if (!active->data()->move_focus_up(focus_wrap))
                return false;
            break;
        case Direction::Down:
            if (!active->data()->move_focus_down(focus_wrap))
                return false;
            break;
        case Direction::Begin:
            move_focus_begin();
            break;
        case Direction::End:
            move_focus_end();
            break;
        default:
            return true;
        }
        recalculate_row_geometry();
        return true;
    }

private:
    bool move_focus_left(bool focus_wrap) {
        if (active == columns.first()) {
            PHLMONITOR monitor = g_pCompositor->getMonitorInDirection(Math::fromChar('l'));
            if (monitor == nullptr) {
                if (focus_wrap)
                    active = columns.last();
                return true;
            }

            g_pKeybindManager->m_dispatchers["movefocus"]("l");
            return false;
        }
        active = active->prev();
        return true;
    }
    bool move_focus_right(bool focus_wrap) {
        if (active == columns.last()) {
            PHLMONITOR monitor = g_pCompositor->getMonitorInDirection(Math::fromChar('r'));
            if (monitor == nullptr) {
                if (focus_wrap)
                    active = columns.first();
                return true;
            }

            g_pKeybindManager->m_dispatchers["movefocus"]("r");
            return false;
        }
        active = active->next();
        return true;
    }
    void move_focus_begin() {
        active = columns.first();
    }
    void move_focus_end() {
        active = columns.last();
    }

    // Calculate lateral gaps for a column
    Vector2D calculate_gap_x(const ListNode<Column *> *column) const {
        // First and last columns need a different gap
        auto gap0 = column == columns.first() ? 0.0 : gap;
        auto gap1 = column == columns.last() ? 0.0 : gap;
        return Vector2D(gap0, gap1);
    }

public:
    void resize_active_column(int step) {
        if (active->data()->maximized())
            return;

        if (mode == Mode::Column) {
            active->data()->cycle_size_active_window(step, calculate_gap_x(active), gap);
            return;
        }

        ColumnWidth width = active->data()->get_width();
        if (width == ColumnWidth::Free) {
            // When cycle-resizing from Free mode, always move back to OneHalf
            width = ColumnWidth::OneHalf;
        } else {
            int number = static_cast<int>(ColumnWidth::Number);
            width =static_cast<ColumnWidth>(
                    (number + static_cast<int>(width) + step) % number);
        }
        active->data()->update_width(width, max.w, max.h);
        reorder = Reorder::Auto;
        recalculate_row_geometry();
    }
    void resize_active_window(const Vector2D &delta) {
        // If the active window in the active column is fullscreen, ignore.
        if (active->data()->maximized() ||
            active->data()->fullscreen())
            return;

        active->data()->resize_active_window(max.w, calculate_gap_x(active), gap, delta);
        recalculate_row_geometry();
    }
    void set_mode(Mode m) {
        mode = m;
    }
    void align_column(Direction dir) {
        if (active->data()->maximized() ||
            active->data()->fullscreen())
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
private:
    void center_active_column() {
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
public:
    void move_active_column(Direction dir) {
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
    void admit_window_left() {
        if (active->data()->maximized() ||
            active->data()->fullscreen())
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
    void expel_window_right() {
        if (active->data()->maximized() ||
            active->data()->fullscreen())
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
#if 1
        double maxw = width == ColumnWidth::Free ? active->data()->get_geom_w() : max.w;
#else
        double maxw = max.w;
        if (width == ColumnWidth::Free)
            width = ColumnWidth::OneHalf;
#endif
        active = columns.emplace_after(active, new Column(w, width, maxw, max.h));
        // Initialize the position so it is located after its previous column
        // This helps the heuristic in recalculate_row_geometry()
        active->data()->set_geom_pos(active->prev()->data()->get_geom_x() + active->prev()->data()->get_geom_w(), max.y);

        reorder = Reorder::Auto;
        recalculate_row_geometry();
    }
    Vector2D predict_window_size() const {
        return Vector2D(0.5 * max.w, max.h);
    }
    void update_sizes(PHLMONITOR monitor) {
        // for gaps outer
        static auto PGAPSINDATA = CConfigValue<Hyprlang::CUSTOMTYPE>("general:gaps_in");
        static auto PGAPSOUTDATA = CConfigValue<Hyprlang::CUSTOMTYPE>("general:gaps_out");
        auto *const PGAPSIN = (CCssGapData *)(PGAPSINDATA.ptr())->getData();
        auto *const PGAPSOUT = (CCssGapData *)(PGAPSOUTDATA.ptr())->getData();
        // For now, support only constant CCssGapData
        auto gaps_in = PGAPSIN->m_top;
        auto gaps_out = PGAPSOUT->m_top;

        const auto reserved = monitor->m_reservedArea;
        const auto gapOutTopLeft  = Vector2D(reserved.left(), reserved.top());
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
    void set_fullscreen_active_window() {
        active->data()->set_fullscreen(full);
        // Parameters here don't matter
        active->data()->recalculate_col_geometry(calculate_gap_x(active), gap);
    }
    void toggle_fullscreen_active_window() {
        Column *column = active->data();
        const auto PWORKSPACE = g_pCompositor->getWorkspaceByID(
            column->get_active_window()->workspaceID());

        auto fullscreen = active->data()->toggle_fullscreen(full);
        PWORKSPACE->m_hasFullscreenWindow = fullscreen;

        if (fullscreen) {
            PWORKSPACE->m_fullscreenMode = FSMODE_FULLSCREEN;
            column->recalculate_col_geometry(calculate_gap_x(active), gap);
        } else {
            recalculate_row_geometry();
        }
    }
    void toggle_maximize_active_column() {
        Column *column = active->data();
        column->toggle_maximized(max.w, max.h);
        reorder = Reorder::Auto;
        recalculate_row_geometry();
    }

    void fit_size(FitSize fitsize) {
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
                    //should never happen as columns are never wider than the screen
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
                    //should never happen as columns are never wider than the screen
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
        // screen (max.w) among from->to, and readapt the rest
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
    void toggle_overview() {
        overview = !overview;
        if (overview) {
            // Find the bounding box
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

    void recalculate_row_geometry() {
        if (active == nullptr)
            return;

        if (active->data()->fullscreen()) {
            active->data()->recalculate_col_geometry(calculate_gap_x(active), gap);
            return;
        }
#ifdef COLORS_IPC
        // Change border color
    	static auto *const FREECOLUMN = (CGradientValueData *) HyprlandAPI::getConfigValue(PHANDLE, "plugin:scroller:col.freecolumn_border")->data.get();
        static auto *const ACTIVECOL = (CGradientValueData *)g_pConfigManager->getConfigValuePtr("general:col.active_border")->data.get();
        if (active->data()->get_width() == ColumnWidth::Free) {
            active->data()->get_active_window()->m_cRealBorderColor = *FREECOLUMN;
        } else {
            active->data()->get_active_window()->m_cRealBorderColor = *ACTIVECOL;
        }
        g_pEventManager->postEvent(SHyprIPCEvent{"scroller", active->data()->get_width_name() + "," + active->data()->get_height_name()});
#endif
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
            } else {
                // first window, locate it at the center
                a_x = max.x + 0.5 * (max.w - a_w);
            }
            // mark column as initialized
            active->data()->set_init();
        }
        if (a_x < max.x) {
            // active starts outside on the left
            // set it on the left edge
            active->data()->set_geom_pos(max.x, max.y);
        } else if (std::round(a_x + a_w) > max.x + max.w) {
            // active overflows to the right, move to end of viewport
            active->data()->set_geom_pos(max.x + max.w - a_w, max.y);
        } else {
            // active is inside the viewport
            if (reorder == Reorder::Auto) {
                // The active column should always be completely in the viewport.
                // If any of the windows next to it on its right or left are
                // in the viewport, keep the current position.
                bool keep_current = false;
                if (active->prev() != nullptr) {
                    Column *prev = active->prev()->data();
                    if (prev->get_geom_x() >= max.x && prev->get_geom_x() + prev->get_geom_w() <= max.x + max.w) {
                        keep_current = true;
                    }
                }
                if (!keep_current && active->next() != nullptr) {
                    Column *next = active->next()->data();
                    if (next->get_geom_x() >= max.x && next->get_geom_x() + next->get_geom_w() <= max.x + max.w) {
                        keep_current = true;
                    }
                }
                if (!keep_current) {
                    // If not:
                    // We try to fit the column next to it on the right if it fits
                    // completely, otherwise the one on the left. If none of them fit,
                    // we leave it as it is.
                    if (active->next() != nullptr) {
                        if (a_w + active->next()->data()->get_geom_w() <= max.w) {
                            // set next at the right edge of the viewport
                            active->data()->set_geom_pos(max.x + max.w - a_w - active->next()->data()->get_geom_w(), max.y);
                        } else if (active->prev() != nullptr) {
                            if (active->prev()->data()->get_geom_w() + a_w <= max.w) {
                                // set previous at the left edge of the viewport
                                active->data()->set_geom_pos(max.x + active->prev()->data()->get_geom_w(), max.y);
                            } else {
                                // none of them fit, leave active as it is
                                active->data()->set_geom_pos(a_x, max.y);
                            }
                        } else {
                            // nothing on the left, move active to left edge of viewport
                            active->data()->set_geom_pos(max.x, max.y);
                        }
                    } else if (active->prev() != nullptr) {
                        if (active->prev()->data()->get_geom_w() + a_w <= max.w) {
                            // set previous at the left edge of the viewport
                            active->data()->set_geom_pos(max.x + active->prev()->data()->get_geom_w(), max.y);
                        } else {
                            // it doesn't fit and nothing on the right, move active to right edge of viewport
                            active->data()->set_geom_pos(max.x + max.w - a_w, max.y);
                        }
                    } else {
                        // nothing on the right or left, the window is in a correct position
                        active->data()->set_geom_pos(a_x, max.y);
                    }
                } else {
                    // the window is in a correct position
                    active->data()->set_geom_pos(a_x, max.y);
                }
            } else {  // lazy
                // Try to avoid moving the active column unless it is out of the screen.
                // the window is in a correct position
                active->data()->set_geom_pos(a_x, max.y);
            }
        }

        adjust_columns(active);
    }

private:
    // Adjust all the columns in the row using 'column' as anchor
    void adjust_columns(ListNode<Column *> *column) {
        // Adjust the positions of the columns to the left
        for (auto col = column->prev(), prev = column; col != nullptr; prev = col, col = col->prev()) {
            col->data()->set_geom_pos(prev->data()->get_geom_x() - col->data()->get_geom_w(), max.y);
        }
        // Adjust the positions of the columns to the right
        for (auto col = column->next(), prev = column; col != nullptr; prev = col, col = col->next()) {
            col->data()->set_geom_pos(prev->data()->get_geom_x() + prev->data()->get_geom_w(), max.y);
        }

        // Apply column geometry
        for (auto col = columns.first(); col != nullptr; col = col->next()) {
            // First and last columns need a different gap
            auto gap0 = col == columns.first() ? 0.0 : gap;
            auto gap1 = col == columns.last() ? 0.0 : gap;
            col->data()->recalculate_col_geometry(Vector2D(gap0, gap1), gap);
        }
    }

    int workspace;
    Box full;
    Box max;
    bool overview;
    int gap;
    Reorder reorder;
    Mode mode;
    ListNode<Column *> *active;
    List<Column *> columns;
};


Row *ScrollerLayout::getRowForWorkspace(int workspace) {
    // Linear lookup because row count is typically small (per workspace list).
    for (auto row = rows.first(); row != nullptr; row = row->next()) {
        if (row->data()->get_workspace() == workspace)
            return row->data();
    }
    return nullptr;
}

Row *ScrollerLayout::getRowForWindow(PHLWINDOW window) {
    // Walk rows and let columns report membership by pointer equality.
    for (auto row = rows.first(); row != nullptr; row = row->next()) {
        if (row->data()->has_window(window))
            return row->data();
    }
    return nullptr;
}

void ScrollerLayout::newTarget(SP<Layout::ITarget> target) {
    // Called by CLayout::addTarget; add every new tiled target using default placement.
    if (!target)
        return;

    auto window = target->window();
    if (!window)
        return;

    onWindowCreatedTiling(window, Math::DIRECTION_DEFAULT);
}

void ScrollerLayout::movedTarget(SP<Layout::ITarget> target, std::optional<Vector2D>)
{
    // Re-use create path to keep placement logic centralized.
    if (!target)
        return;

    auto window = target->window();
    if (!window)
        return;

    onWindowCreatedTiling(window, Math::DIRECTION_DEFAULT);
}

void ScrollerLayout::removeTarget(SP<Layout::ITarget> target)
{
    // Remove target from in-memory row/column model on unmap/close.
    if (!target)
        return;

    onWindowRemovedTiling(target->window());
}

void ScrollerLayout::resizeTarget(const Vector2D &delta, SP<Layout::ITarget> target, Layout::eRectCorner)
{
    // If the window is not managed by a row, resize the real animated size directly.
    auto window = windowFromTarget(target);
    if (!window)
        return;

    auto s = getRowForWindow(window);
    if (s == nullptr) {
        if (window->m_realSize)
            *window->m_realSize = Vector2D(std::max((window->m_realSize->goal() + delta).x, 20.0), std::max((window->m_realSize->goal() + delta).y, 20.0));
        window->updateWindowDecos();
        return;
    }

    s->focus_window(window);
    s->resize_active_window(delta);
}

void ScrollerLayout::recalculate()
{
    // Full layout pass: refresh every row against its current monitor and fullscreen state.
    for (auto row = rows.first(); row != nullptr; row = row->next()) {
        const auto workspace = g_pCompositor->getWorkspaceByID(row->data()->get_workspace());
        if (!workspace)
            continue;
        const auto monitor = g_pCompositor->getMonitorFromID(workspace->monitorID());
        if (!monitor)
            continue;

        row->data()->update_sizes(monitor);
        if (workspace->m_hasFullscreenWindow && workspace->m_fullscreenMode == FSMODE_FULLSCREEN)
            row->data()->set_fullscreen_active_window();
        else
            row->data()->recalculate_row_geometry();
    }
}

std::expected<void, std::string> ScrollerLayout::layoutMsg(const std::string_view&)
{
    // No custom layout message channel is implemented yet.
    return {};
}

std::optional<Vector2D> ScrollerLayout::predictSizeForNewTarget()
{
    // Predicts geometry for new tiled window creation on active monitor workspace.
    auto monitor = monitorFromPointingOrCursor();
    if (!monitor)
        return {};

    auto row = getRowForWorkspace(monitor->activeWorkspaceID());
    if (!row)
        return Vector2D(monitor->m_size.x * 0.5, monitor->m_size.y);

    return row->predict_window_size();
}

SP<Layout::ITarget> ScrollerLayout::getNextCandidate(SP<Layout::ITarget> old)
{
    // Keeps cycling behavior stable by returning the current active target when possible.
    int workspace_id = WORKSPACE_INVALID;
    if (auto oldWindow = windowFromTarget(old))
        workspace_id = oldWindow->workspaceID();
    if (workspace_id == WORKSPACE_INVALID) {
        auto monitor = monitorFromPointingOrCursor();
        if (monitor)
            workspace_id = monitor->activeWorkspaceID();
    }

    auto s = getRowForWorkspace(workspace_id);
    if (!s)
        return {};

    const auto active = s->get_active_window();
    if (!active)
        return {};

    return active->layoutTarget();
}

void ScrollerLayout::swapTargets(SP<Layout::ITarget> a, SP<Layout::ITarget> b)
{
    // Only swap within one row; no cross-row move is supported for this layout.
    auto wa = windowFromTarget(a);
    auto wb = windowFromTarget(b);
    auto sa = getRowForWindow(wa);
    auto sb = getRowForWindow(wb);
    if (!wa || !wb || !sa || !sb || sa != sb)
        return;

    sa->swapWindows(wa, wb);
}

void ScrollerLayout::moveTargetInDirection(SP<Layout::ITarget> t, Math::eDirection direction, bool)
{
    // Map compositor direction into dispatcher-level focus+move behavior.
    auto window = windowFromTarget(t);
    auto s = getRowForWindow(window);
    if (!s || !window)
        return;

    s->focus_window(window);
    switch (direction) {
        case Math::DIRECTION_LEFT:
            s->move_active_column(Direction::Left);
            break;
        case Math::DIRECTION_RIGHT:
            s->move_active_column(Direction::Right);
            break;
        case Math::DIRECTION_UP:
            s->move_active_column(Direction::Up);
            break;
    case Math::DIRECTION_DOWN:
        s->move_active_column(Direction::Down);
        break;
    default:
        return;
    }
}

/*
    Called when a window is created (mapped)
    The layout HAS TO set the goal pos and size (anim mgr will use it)
    If !animationinprogress, then the anim mgr will not apply an anim.
*/
void ScrollerLayout::onWindowCreatedTiling(PHLWINDOW window, Math::eDirection)
{
    auto s = getRowForWorkspace(window->workspaceID());
    if (s == nullptr) {
        s = new Row(window);
        rows.push_back(s);
    }
    s->add_active_window(window);
}

/*
    Called when a window is removed (unmapped)
*/
void ScrollerLayout::onWindowRemovedTiling(PHLWINDOW window)
{
    marks.remove(window);

    auto s = getRowForWindow(window);
    if (s == nullptr) {
        return;
    }
    if (!s->remove_window(window)) {
        // It was the last one, remove the row
        for (auto row = rows.first(); row != nullptr; row = row->next()) {
            if (row->data() == s) {
                rows.erase(row);
                delete row->data();
                return;
            }
        }
    }
}

/*
    Internal: called when window focus changes
*/
void ScrollerLayout::onWindowFocusChange(PHLWINDOW window)
{
    if (window == nullptr) { // no window has focus
        return;
    }

    auto s = getRowForWindow(window);
    if (s == nullptr) {
        return;
    }
    s->focus_window(window);
}

/*
    Return tiled status
*/
bool ScrollerLayout::isWindowTiled(PHLWINDOW window)
{
    return getRowForWindow(window) != nullptr;
}

/*
    Called when the monitor requires a layout recalculation
    this usually means reserved area changes
*/
void ScrollerLayout::recalculateMonitor(const int &monitor_id)
{
    auto PMONITOR = g_pCompositor->getMonitorFromID(monitor_id);
    if (!PMONITOR)
        return;

    g_pHyprRenderer->damageMonitor(PMONITOR);

    auto PWORKSPACE = PMONITOR->m_activeWorkspace;
    if (!PWORKSPACE)
        return;

    auto s = getRowForWorkspace(PWORKSPACE->m_id);
    if (s == nullptr)
        return;

    s->update_sizes(PMONITOR);
    if (PWORKSPACE->m_hasFullscreenWindow && PWORKSPACE->m_fullscreenMode == FSMODE_FULLSCREEN) {
        s->set_fullscreen_active_window();
    } else {
        s->recalculate_row_geometry();
    }
    if (PMONITOR->activeSpecialWorkspaceID()) {
        auto sw = getRowForWorkspace(PMONITOR->activeSpecialWorkspaceID());
        if (sw == nullptr) {
            return;
        }
        sw->update_sizes(PMONITOR);
        sw->recalculate_row_geometry();
    }
}

/*
    Called when the compositor requests a window
    to be recalculated, e.g. when pseudo is toggled.
*/
void ScrollerLayout::recalculateWindow(PHLWINDOW window)
{
    auto s = getRowForWindow(window);
    if (s == nullptr)
        return;

    s->recalculate_row_geometry();
}

/*
    Called when a user requests a resize of the current window by a vec
    Vector2D holds pixel values
    Optional pWindow for a specific window
*/
void ScrollerLayout::resizeActiveWindow(PHLWINDOW window, const Vector2D &delta,
                                        Layout::eRectCorner, PHLWINDOW pWindow)
{
    const auto PWINDOW = pWindow ? pWindow : window;
    if (!PWINDOW)
        return;

    auto s = getRowForWindow(PWINDOW);
    if (s == nullptr) {
        // Window is not tiled
        if (PWINDOW->m_realSize)
            *PWINDOW->m_realSize = Vector2D(std::max((PWINDOW->m_realSize->goal() + delta).x, 20.0), std::max((PWINDOW->m_realSize->goal() + delta).y, 20.0));
        PWINDOW->updateWindowDecos();
        return;
    }

    s->resize_active_window(delta);
}

// Called when a move command targets a specific tiled window via legacy API.
void ScrollerLayout::moveWindowTo(PHLWINDOW window, const std::string &direction, bool silent)
{
    auto s = getRowForWindow(window);
    if (s == nullptr) {
        return;
    } else if (!(s->is_active(window))) {
        // cannot move non active window?
        return;
    }

    switch (direction.at(0)) {
        case 'l': s->move_active_column(Direction::Left); break;
        case 'r': s->move_active_column(Direction::Right); break;
        case 'u': s->move_active_column(Direction::Up); break;
        case 'd': s->move_active_column(Direction::Down); break;
        default: break;
    }

    // "silent" requires to keep focus in the neighborhood of the moved window
    // before it moved. I ignore it for now.
}

// Compatibility hook for split ratio changes; no-op for this layout.
void ScrollerLayout::alterSplitRatio(PHLWINDOW, float, bool)
{
}

// Rebuild in-memory state from existing mapped tiled windows.
void ScrollerLayout::onEnable() {
    marks.reset();
    for (auto& window : g_pCompositor->m_windows) {
        if (window->m_isFloating || !window->m_isMapped || window->isHidden())
            continue;

        onWindowCreatedTiling(window);
        recalculateMonitor(window->monitorID());
    }
}

// Drop all cached rows and marks when plugin is disabled.
void ScrollerLayout::onDisable() {
    for (auto row = rows.first(); row != nullptr; row = row->next()) {
        delete row->data();
    }
    rows.clear();
    marks.reset();
}

/*
    Called to predict the size of a newly opened window to send it a configure.
    Return 0,0 if unpredictable
*/
Vector2D ScrollerLayout::predictSizeForNewWindowTiled() {
    auto monitor = monitorFromPointingOrCursor();
    if (!monitor)
        return {};

    int workspace_id = monitor->activeWorkspaceID();
    auto s = getRowForWorkspace(workspace_id);
    if (s == nullptr) {
        Vector2D size = monitor->m_size;
        size.x *= 0.5;
        return size;
    }

    return s->predict_window_size();
}

// Dispatcher wrapper: grow/shrink active row or column sizing.
void ScrollerLayout::cycle_window_size(int workspace, int step)
{
    auto s = getRowForWorkspace(workspace);
    if (s == nullptr) {
        return;
    }

    s->resize_active_column(step);
}

// Focus a window and force mouse/focus state sync so pointer-driven workflows work.
static void switch_to_window(PHLWINDOW window)
{
    if (!window || g_pCompositor->isWindowActive(window))
        return;

    g_pInputManager->unconstrainMouse();
    window->activate();
    g_pCompositor->warpCursorTo(window->middle());

    g_pInputManager->m_forcedFocus = window;
    g_pInputManager->simulateMouseMovement();
    g_pInputManager->m_forcedFocus.reset();
}

void ScrollerLayout::move_focus(int workspace, Direction direction)
{
    static auto* const *focus_wrap = (Hyprlang::INT* const *)HyprlandAPI::getConfigValue(PHANDLE, "plugin:scroller:focus_wrap")->getDataStaticPtr();
    auto s = getRowForWorkspace(workspace);
    if (s == nullptr) {
        // if workspace is empty, use the deault movefocus, which now
        // is "move to another monitor" (pass the direction)
        switch (direction) {
            case Direction::Left:
                g_pKeybindManager->m_dispatchers["movefocus"]("l");
                break;
            case Direction::Right:
                g_pKeybindManager->m_dispatchers["movefocus"]("r");
                break;
            case Direction::Up:
                g_pKeybindManager->m_dispatchers["movefocus"]("u");
                break;
            case Direction::Down:
                g_pKeybindManager->m_dispatchers["movefocus"]("d");
                break;
            default:
                break;
        }
        return;
    }

    if (!s->move_focus(direction, **focus_wrap == 0 ? false : true)) {
        // changed monitor
        auto monitor = monitorFromPointingOrCursor();
        const auto workspaceId = monitor ? monitor->activeWorkspaceID() : WORKSPACE_INVALID;
        s = getRowForWorkspace(workspaceId);
        if (s == nullptr) {
            // monitor is empty
            return;
        }
    }

    switch_to_window(s->get_active_window());
}

// Compatibility hook kept for API symmetry; no metadata replacement is needed.
void ScrollerLayout::replaceWindowDataWith(PHLWINDOW, PHLWINDOW)
{
}

// Reserved API for scripted reset-height behavior; intentionally not implemented.
void ScrollerLayout::reset_height(int)
{
}

void ScrollerLayout::move_window(int workspace, Direction direction) {
    auto s = getRowForWorkspace(workspace);
    if (s == nullptr) {
        return;
    }

    s->move_active_column(direction);
    switch_to_window(s->get_active_window());
}

void ScrollerLayout::align_window(int workspace, Direction direction) {
    auto s = getRowForWorkspace(workspace);
    if (s == nullptr) {
        return;
    }

    s->align_column(direction);
}

void ScrollerLayout::admit_window_left(int workspace) {
    auto s = getRowForWorkspace(workspace);
    if (s == nullptr) {
        return;
    }
    s->admit_window_left();
}

void ScrollerLayout::expel_window_right(int workspace) {
    auto s = getRowForWorkspace(workspace);
    if (s == nullptr) {
        return;
    }
    s->expel_window_right();
}

void ScrollerLayout::set_mode(int workspace, Mode mode) {
    auto s = getRowForWorkspace(workspace);
    if (s == nullptr) {
        return;
    }
    s->set_mode(mode);
}

void ScrollerLayout::fit_size(int workspace, FitSize fitsize) {
    auto s = getRowForWorkspace(workspace);
    if (s == nullptr) {
        return;
    }
    s->fit_size(fitsize);
}

void ScrollerLayout::toggle_overview(int workspace) {
    auto s = getRowForWorkspace(workspace);
    if (s == nullptr) {
        return;
    }
    s->toggle_overview();
}

static int get_workspace_id() {
    int workspace_id;
    auto monitor = monitorFromPointingOrCursor();
    if (!monitor)
        return -1;

    if (monitor->activeSpecialWorkspaceID()) {
        workspace_id = monitor->activeSpecialWorkspaceID();
    } else {
        workspace_id = monitor->activeWorkspaceID();
    }
    if (workspace_id == WORKSPACE_INVALID)
        return -1;
    if (g_pCompositor->getWorkspaceByID(workspace_id) == nullptr)
        return -1;

    return workspace_id;
}

void ScrollerLayout::marks_add(const std::string &name) {
    auto workspace = getRowForWorkspace(get_workspace_id());
    if (!workspace)
        return;

    PHLWINDOW w = workspace->get_active_window();
    if (!w)
        return;

    marks.add(w, name);
}

void ScrollerLayout::marks_delete(const std::string &name) {
    marks.del(name);
}

void ScrollerLayout::marks_visit(const std::string &name) {
    PHLWINDOW window = marks.visit(name);
    if (window != nullptr)
        switch_to_window(window);
}

void ScrollerLayout::marks_reset() {
    marks.reset();
}
