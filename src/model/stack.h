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

#include <memory>
#include <string>
#include <utility>

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

/**
 * @brief Lightweight model wrapper around a compositor window.
 *
 * `Window` stores the logical vertical geometry used by the scrolling model.
 * It intentionally does not own lane/canvas placement concerns; its job is to
 * remember the per-window height policy and the logical Y/H values that stack
 * relayout operates on.
 */
class Window {
public:
    // Construct model wrapper for a backend window and its initial logical geometry.
    Window(PHLWINDOW window, double box_h);
    // Access original compositor window handle.
    PHLWINDOWREF ptr() const;
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
    Stack(PHLWINDOW cwindow, double maxw, double maxh, Mode mode);
    // Build a new stack from an existing model window when splitting.
    Stack(std::unique_ptr<Window> window, StackWidth width, double maxw, double maxh, Mode mode);
    // Destroy all windows in this stack.
    ~Stack();

    // Initialization state is used for first-time placement logic.
    bool get_init() const;
    void set_init();
    // Number of windows in this stack.
    size_t size() const;

    // Window membership / reorder helpers.
    bool has_window(PHLWINDOW window) const;
    bool swap_windows(PHLWINDOW a, PHLWINDOW b);
    template <typename Fn>
    void for_each_window(Fn&& fn) const {
        for (auto win = windows.first(); win != nullptr; win = win->next()) {
            if (auto window = win->data()->ptr().lock())
                std::forward<Fn>(fn)(window);
        }
    }
    // Remove a window and keep active pointer coherent.
    void remove_window(PHLWINDOW window);
    // Move active pointer to the matching model window.
    void focus_window(PHLWINDOW window);

    // Geometry accessors used by layout composition.
    double get_geom_x() const;
    double get_geom_y() const;
    double get_geom_w() const;
    double get_geom_h() const;
    // Mutate current stack width only; callers must recalc afterwards.
    void set_geom_w(double w);
    void set_geom_h(double h);
    // Return vertical bounds (top of first and bottom of last rendered window).
    Vector2D get_height() const;

    // Apply relative scale to all windows in this stack.
    void scale(const Vector2D &bmin, const Vector2D &start, double scale, double gap);
    // Toggle fullscreen state request and report the target fullscreen flag.
    bool toggle_fullscreen(const ScrollerCore::Box &fullbbox);
    // Set fullscreen target bbox for internal bookkeeping.
    void set_fullscreen(const ScrollerCore::Box &fullbbox);
    // Return true when stack-managed fullscreen is active.
    bool expanded() const;
    // Snapshot/restore geometry for minimize-disruptive transforms.
    void push_geom();
    void pop_geom();
    // Toggle maximized mode and preserve/restore active window geometry.
    void toggle_maximized(double maxw, double maxh);

    bool fullscreen() const;
    bool maximized() const;
    Mode get_mode() const;
    void set_mode(Mode mode, double maxw, double maxh);
    void shift_local_geometry(double delta);
    // Set absolute x/y placement of the stack.
    void set_geom_pos(double x, double y);

    // Recompute active-window geometry and propagate updates to siblings.
    void recalculate_stack_geometry(const Vector2D &gap_x, double gap);
    // Return currently active compositor window.
    PHLWINDOW get_active_window();
    // Return whether the active model window is already at a stack edge.
    bool active_at_edge(Direction direction) const;
    // Move active model window inside the same stack list.
    void move_active(Direction direction);
    // Focus movement with wrap behavior across monitor edges.
    FocusMoveResult move_focus(Direction direction, bool focus_wrap);

    // Insert a model window whose ownership has been transferred to this stack.
    void admit_window(std::unique_ptr<Window> window);
    // Restore a previously extracted active window next to the current active one.
    void restore_window(std::unique_ptr<Window> window, bool insertBeforeActive);
    // Remove the active model window and transfer ownership to the caller as a unique owner.
    std::unique_ptr<Window> expel_active(double gap);
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
    // Resize width and optional active height if height delta is valid.
    void resize_active_window(double maxw, const Vector2D &gap_x, double gap, const Vector2D &delta);

private:
    // Find the list node that owns a given compositor window.
    ListNode<Window *> *findWindowNode(PHLWINDOW window) const;
    // Shift a window range so the active window stays visible inside the stack viewport.
    void adjust_windows(ListNode<Window *> *win, const Vector2D &gap_x, double gap);

    // Restore point for stack-level geometry transforms.
    struct Memory {
        ScrollerCore::Box geom;
    };

    // Current horizontal width mode of the stack.
    StackWidth width;
    // Orientation profile used for stack-vs-window axis decisions.
    Mode mode;
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
    ListNode<Window *> *active = nullptr;
    // Ordered windows inside this stack.
    List<Window *> windows;
};

} // namespace ScrollerModel
