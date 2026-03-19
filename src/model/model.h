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
 * The row/controller layer composes these primitives to implement
 * workspace-level navigation and monitor integration.
 */
#pragma once

#include <string>

#include <hyprutils/math/Vector2D.hpp>

#include "../list.h"
#include "../core/core.h"
#include "../layout/scroller_layout.h"

namespace ScrollerModel {

enum class ColumnWidth {
    // Predefined proportional width presets used when creating or cycling columns.
    OneThird = 0,
    // Exactly half of available workspace width.
    OneHalf,
    // Two-thirds of available workspace width.
    TwoThirds,
    // Sentinal used to count user-defined width states.
    Number,
    // Keep column at explicit width (free mode or carried-over width).
    Free
};

enum class WindowHeight {
    // Common per-window ratios for column heights.
    OneThird,
    OneHalf,
    TwoThirds,
    One,
    // Keep Number as the last standard cycling state.
    Number,
    // Window keeps a user-defined height and is not affected by preset cycling.
    Free,
    // Compatibility value used by optional IPC hooks / fallbacks.
    Auto
};

enum class Reorder {
    // Automatic layout may move windows to preserve visibility.
    Auto,
    // Preserve current order, avoid aggressive repositioning unless needed.
    Lazy
};

// Internal window wrapper used by Column to keep geometry, history and height mode.
class Window {
public:
    // Construct model wrapper for a backend window and its initial logical geometry.
    Window(PHLWINDOW window, double box_h);
    // Access original compositor window handle.
    PHLWINDOWREF ptr();
    // Return logical geometry height used by scroller model.
    double get_geom_h() const;
    // Store logical geometry height used by layout calculations.
    void set_geom_h(double geom_h);
    // Save current geometry values into a lightweight undo buffer.
    void push_geom();
    // Restore geometry values from the undo buffer.
    void pop_geom();
    // Current height mode used for cycle logic.
    WindowHeight get_height() const;
    // Change height mode and sync the logical height for this mode.
    void update_height(WindowHeight h, double max);
    // Switch to free (custom) height mode.
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
    // Build a new column from a compositor window with configuration defaults.
    Column(PHLWINDOW cwindow, double maxw, double maxh);
    // Build a new column from an existing model window when splitting.
    Column(Window *window, ColumnWidth width, double maxw, double maxh);
    // Destroy all windows in this column.
    ~Column();

    // Initialization state is used for first-time placement logic.
    bool get_init() const;
    void set_init();
    // Number of windows in this column.
    size_t size();

    // Window membership / reorder helpers.
    bool has_window(PHLWINDOW window) const;
    bool swap_windows(PHLWINDOW a, PHLWINDOW b);
    // Insert a new window and make it active.
    void add_active_window(PHLWINDOW window, double maxh);
    // Remove a window and keep active pointer coherent.
    void remove_window(PHLWINDOW window);
    // Move active pointer to the matching model window.
    void focus_window(PHLWINDOW window);

    // Geometry accessors used by layout composition.
    double get_geom_x() const;
    double get_geom_w() const;
    // Mutate current column width only; callers must recalc afterwards.
    void set_geom_w(double w);
    // Return vertical bounds (top of first and bottom of last rendered window).
    Vector2D get_height() const;

    // Apply relative scale to all windows in this column.
    void scale(const Vector2D &bmin, const Vector2D &start, double scale, double gap);
    // Toggle fullscreen state request and report the target fullscreen flag.
    bool toggle_fullscreen(const ScrollerCore::Box &fullbbox, Mode mode);
    // Set fullscreen target bbox for internal bookkeeping.
    void set_fullscreen(const ScrollerCore::Box &fullbbox);
    // Return true when scroller-specific expansion is active.
    bool expanded() const;
    // Snapshot/restore geometry for minimize-disruptive transforms.
    void push_geom();
    void pop_geom();
    // Toggle maximized mode and preserve/restore active window geometry.
    void toggle_maximized(double maxw, double maxh);

    bool fullscreen() const;
    bool maximized() const;
    // Set absolute x/y placement of the column.
    void set_geom_pos(double x, double y);

    // Recompute active-window geometry and propagate updates to siblings.
    void recalculate_col_geometry(const Vector2D &gap_x, double gap);
    // Return currently active compositor window.
    PHLWINDOW get_active_window();
    // Move active model window inside the same column list.
    void move_active_up();
    void move_active_down();
    // Focus movement with wrap behavior across monitor edges.
    bool move_focus_up(bool focus_wrap);
    bool move_focus_down(bool focus_wrap);

    // Insert/remove window while keeping active tracking consistent.
    void admit_window(Window *window);
    Window *expel_active(double gap);
    // Move active window toward viewport edges/center inside the current column.
    void align_window(Direction direction, double gap);

    // Width and height mode inspection + mutation.
    ColumnWidth get_width() const;
    void set_width_free();
#ifdef COLORS_IPC
    std::string get_width_name() const;
    std::string get_height_name() const;
#endif

    // Update column width from a predefined mode and current monitor bounds.
    void update_width(ColumnWidth cwidth, double maxw, double maxh);
    // Resize a window range (all/visible/active/to ends) to fill available height.
    void fit_size(FitSize fitsize, const Vector2D &gap_x, double gap);
    // Cycle active window logical height and recompute geometry.
    void cycle_size_active_window(int step, const Vector2D &gap_x, double gap);
    // Resize width and optional active height if height delta is valid.
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
    bool fullscreened = false;
    ListNode<Window *> *fullscreen_window = nullptr;
    bool maxdim;
    Memory mem;
    ScrollerCore::Box full;
    ListNode<Window *> *active;
    List<Window *> windows;
};

} // namespace ScrollerModel
