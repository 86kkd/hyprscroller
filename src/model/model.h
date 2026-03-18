#pragma once

/**
 * @file model.h
 * @brief Core model layer for scroller layout state.
 *
 * This module owns the lightweight in-memory objects that represent
 * tiled layout data independent of monitor/row orchestration.
 * `Window` stores per-window geometry state (logical height, cached
 * positions, and height mode), while `Column` manages an ordered stack
 * of windows and all column-level layout math for movement, resizing,
 * fullscreen/maximized behavior, and alignment.
 *
 * The row/controller layer (in scroller.cpp) composes these primitives
 * to implement workspace-level navigation and monitor integration.
 */

#include <string>

#include <hyprutils/math/Vector2D.hpp>

#include "../list.h"
#include "../core/core.h"
#include "../layout/scroller_layout.h"

namespace ScrollerModel {

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
    // Keep Number as the last standard cycling state.
    Number,
    Free,
    // Compatibility value used by optional IPC hooks.
    Auto
};

enum class Reorder {
    Auto,
    Lazy
};

// Internal window wrapper used by Column to keep geometry, history and height mode.
class Window {
public:
    Window(PHLWINDOW window, double box_h);
    PHLWINDOWREF ptr();
    double get_geom_h() const;
    void set_geom_h(double geom_h);
    void push_geom();
    void pop_geom();
    WindowHeight get_height() const;
    void update_height(WindowHeight h, double max);
    void set_height_free();

private:
    struct Memory {
        double pos_y;
        double box_h;
    };

    PHLWINDOWREF window;
    WindowHeight height;
    double box_h;
    Memory mem;
};

// A column is a vertical list of windows sharing horizontal bounds.
class Column {
public:
    Column(PHLWINDOW cwindow, double maxw, double maxh);
    Column(Window *window, ColumnWidth width, double maxw, double maxh);
    ~Column();

    bool get_init() const;
    void set_init();
    size_t size();

    bool has_window(PHLWINDOW window) const;
    bool swap_windows(PHLWINDOW a, PHLWINDOW b);
    void add_active_window(PHLWINDOW window, double maxh);
    void remove_window(PHLWINDOW window);
    void focus_window(PHLWINDOW window);

    double get_geom_x() const;
    double get_geom_w() const;
    void set_geom_w(double w);
    Vector2D get_height() const;

    void scale(const Vector2D &bmin, const Vector2D &start, double scale, double gap);
    bool toggle_fullscreen(const ScrollerCore::Box &fullbbox);
    void set_fullscreen(const ScrollerCore::Box &fullbbox);
    void push_geom();
    void pop_geom();
    void toggle_maximized(double maxw, double maxh);

    bool fullscreen() const;
    bool maximized() const;
    void set_geom_pos(double x, double y);

    void recalculate_col_geometry(const Vector2D &gap_x, double gap);
    PHLWINDOW get_active_window();
    void move_active_up();
    void move_active_down();
    bool move_focus_up(bool focus_wrap);
    bool move_focus_down(bool focus_wrap);

    void admit_window(Window *window);
    Window *expel_active(double gap);
    void align_window(Direction direction, double gap);

    ColumnWidth get_width() const;
    void set_width_free();
#ifdef COLORS_IPC
    std::string get_width_name() const;
    std::string get_height_name() const;
#endif

    void update_width(ColumnWidth cwidth, double maxw, double maxh);
    void fit_size(FitSize fitsize, const Vector2D &gap_x, double gap);
    void cycle_size_active_window(int step, const Vector2D &gap_x, double gap);
    void resize_active_window(double maxw, const Vector2D &gap_x, double gap, const Vector2D &delta);

private:
    void adjust_windows(ListNode<Window *> *win, const Vector2D &gap_x, double gap);

    struct Memory {
        ScrollerCore::Box geom;
    };

    ColumnWidth width;
    WindowHeight height;
    Reorder reorder;
    bool initialized;
    ScrollerCore::Box geom;
    bool maxdim;
    Memory mem;
    ScrollerCore::Box full;
    ListNode<Window *> *active;
    List<Window *> windows;
};

} // namespace ScrollerModel
