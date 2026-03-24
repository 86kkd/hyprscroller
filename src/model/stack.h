/**
 * @file stack.h
 * @brief Core model layer for scroller layout state.
 *
 * This module owns the lightweight in-memory objects that represent
 * tiled layout data independent of monitor/lane orchestration.
 * `Window` stores per-window geometry state (logical height, cached
 * positions, and height mode), while `Stack` manages an ordered stack
 * of windows and all stack-level layout math for movement, resizing,
 * fullscreen/maximized behavior, and alignment.
 *
 * The lane/controller layer composes these primitives to implement
 * workspace-level navigation and monitor integration.
 */
#pragma once

#include <string>

#include <hyprutils/math/Vector2D.hpp>

#include "../list.h"
#include "../core/types.h"
#include "../core/core.h"

namespace ScrollerModel {

enum class StackWidth {
    // Predefined proportional width presets used when creating or cycling stacks.
    OneThird = 0,
    // Exactly half of available workspace width.
    OneHalf,
    // Two-thirds of available workspace width.
    TwoThirds,
    // Sentinal used to count user-defined width states.
    Number,
    // Keep stack at explicit width (free mode or carried-over width).
    Free
};

enum class WindowHeight {
    // Common per-window ratios for stack heights.
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

enum class FocusMoveResult {
    // Focus moved inside the current stack/lane.
    Moved,
    // No focus change happened.
    NoOp,
    // Movement should continue on another monitor.
    CrossMonitor
};

/**
 * @brief Lightweight model wrapper around a compositor window.
 *
 * `Window` stores the logical vertical geometry used by the scrolling model.
 * It intentionally does not own lane/canvas placement concerns; its job is to
 * remember the per-window height policy, temporary expanded state, and the
 * logical Y/H values that stack relayout operates on.
 */
class Window {
public:
    // Construct model wrapper for a backend window and its initial logical geometry.
    Window(PHLWINDOW window, double box_h);
    // Access original compositor window handle.
    PHLWINDOWREF ptr();
    // Return logical geometry height used by scroller model.
    double get_geom_h() const;
    // Return logical geometry top position used by scroller model.
    double get_geom_y() const;
    // Store logical geometry height used by layout calculations.
    void set_geom_h(double geom_h);
    // Store logical geometry top position used by layout calculations.
    void set_geom_y(double geom_y);
    // Save current geometry values into a lightweight undo buffer.
    void push_geom();
    // Restore geometry values from the undo buffer.
    void pop_geom();
    // Toggle window-specific expanded state used by portrait scroller fullscreen.
    bool toggle_expand(double maxh);
    // Return whether this window is currently expanded by scroller.
    bool expanded() const;
    // Current height mode used for cycle logic.
    WindowHeight get_height() const;
    // Change height mode and sync the logical height for this mode.
    void update_height(WindowHeight h, double max);
    // Switch to free (custom) height mode.
    void set_height_free();

private:
    // Minimal restore point used by fullscreen/overview style transforms.
    struct Memory {
        double box_y;
        double box_h;
    };

    // Weak reference to the backend Hyprland window.
    PHLWINDOWREF window;
    // Current logical height preset for resize/cycle commands.
    WindowHeight height;
    // Logical top position inside the owning stack.
    double box_y;
    // Logical height inside the owning stack.
    double box_h;
    // Portrait-only expanded flag used by scroller fullscreen behavior.
    bool is_expanded = false;
    // Last saved logical geometry.
    Memory mem;
};

/**
 * @brief Ordered vertical group of windows sharing one horizontal slot.
 *
 * `Stack` is the lowest layout unit that still performs real geometry work.
 * It owns the ordered windows inside one slot, tracks the active model window,
 * applies width/height policies, and recalculates the stacked window geometry
 * that the lane layer later positions on the canvas.
 */
class Stack {
public:
    // Build a new stack from a compositor window with configuration defaults.
    Stack(PHLWINDOW cwindow, double maxw, double maxh);
    // Build a new stack from an existing model window when splitting.
    Stack(Window *window, StackWidth width, double maxw, double maxh);
    // Destroy all windows in this stack.
    ~Stack();

    // Initialization state is used for first-time placement logic.
    bool get_init() const;
    void set_init();
    // Number of windows in this stack.
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
    // Mutate current stack width only; callers must recalc afterwards.
    void set_geom_w(double w);
    // Return vertical bounds (top of first and bottom of last rendered window).
    Vector2D get_height() const;

    // Apply relative scale to all windows in this stack.
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
    // Set absolute x/y placement of the stack.
    void set_geom_pos(double x, double y);

    // Recompute active-window geometry and propagate updates to siblings.
    void recalculate_stack_geometry(const Vector2D &gap_x, double gap);
    // Return currently active compositor window.
    PHLWINDOW get_active_window();
    // Return whether the active model window is already at a stack edge.
    bool active_at_edge(Direction direction) const;
    // Move active model window inside the same stack list.
    void move_active_up();
    void move_active_down();
    // Focus movement with wrap behavior across monitor edges.
    FocusMoveResult move_focus_up(bool focus_wrap);
    FocusMoveResult move_focus_down(bool focus_wrap);

    // Insert/remove window while keeping active tracking consistent.
    void admit_window(Window *window);
    Window *expel_active(double gap);
    // Move active window toward viewport edges/center inside the current stack.
    void align_window(Direction direction, double gap);

    // Width and height mode inspection + mutation.
    StackWidth get_width() const;
    void set_width_free();
#ifdef COLORS_IPC
    std::string get_width_name() const;
    std::string get_height_name() const;
#endif

    // Update stack width from a predefined mode and current monitor bounds.
    void update_width(StackWidth cwidth, double maxw, double maxh);
    // Resize a window range (all/visible/active/to ends) to fill available height.
    void fit_size(FitSize fitsize, const Vector2D &gap_x, double gap);
    // Cycle active window logical height and recompute geometry.
    void cycle_size_active_window(int step, const Vector2D &gap_x, double gap);
    // Resize width and optional active height if height delta is valid.
    void resize_active_window(double maxw, const Vector2D &gap_x, double gap, const Vector2D &delta);

private:
    // Shift a window range so the active window stays visible inside the stack viewport.
    void adjust_windows(ListNode<Window *> *win, const Vector2D &gap_x, double gap);

    // Restore point for stack-level geometry transforms.
    struct Memory {
        ScrollerCore::Box geom;
    };

    // Current horizontal width mode of the stack.
    StackWidth width;
    // Height preset of the active window when cycling window sizes.
    WindowHeight height;
    // Auto/lazy reorder policy used by viewport adjustments.
    Reorder reorder;
    // Whether this stack already has stable initial geometry.
    bool initialized;
    // Current stack geometry in canvas coordinates.
    ScrollerCore::Box geom;
    // Scroller-managed fullscreen state.
    bool fullscreened = false;
    // Stack-level maximized state.
    bool maxdim;
    // Saved stack geometry for temporary transforms.
    Memory mem;
    // Full monitor box used by fullscreen behavior.
    ScrollerCore::Box full;
    // Currently active model window node.
    ListNode<Window *> *active;
    // Ordered windows inside this stack.
    List<Window *> windows;
};

} // namespace ScrollerModel
