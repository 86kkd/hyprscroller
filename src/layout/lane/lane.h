/**
 * @file lane.h
 * @brief Lane-level workspace controller for scroller layout.
 *
 * `Lane` owns ordered stacks for a single workspace and handles lane-level
 * focus movement, command dispatch behavior, fullscreen/maximize transitions,
 * overview mode and geometry updates.
 */
#pragma once

#include "../../core/core.h"
#include "../../model/stack.h"
#include "../canvas/layout.h"

using namespace ScrollerCore;
using namespace ScrollerModel;

/**
 * @brief Transfer object used when moving an active window between lanes.
 *
 * A moved window needs more than the raw `Window*`: the destination lane also
 * needs the stack width semantics and any free-width value so it can rebuild a
 * destination stack without losing sizing intent.
 */
struct ActiveWindowPayload {
    Window*    window = nullptr;
    StackWidth width = StackWidth::OneHalf;
    double     maxw = 0.0;

    explicit operator bool() const {
        return window != nullptr;
    }
};

class Lane {
    // A lane owns the ordered stacks visible on one canvas strip.
public:
    Lane(PHLWINDOW window);
    Lane(PHLMONITOR monitor, Mode mode);
    Lane(Stack *stack);
    ~Lane();

    // Structural and state queries.
    bool empty() const;
    bool is_single_window_lane() const;
    Mode get_mode() const;
    bool is_ephemeral() const;
    void set_ephemeral(bool value);
    bool has_window(PHLWINDOW window) const;
    PHLWINDOW get_active_window() const;
    bool is_active(PHLWINDOW window) const;

    // Window/stack membership changes.
    void add_active_window(PHLWINDOW window);
    Stack *extract_active_stack();
    // Remove the active window and return the payload needed to insert it elsewhere.
    ActiveWindowPayload extract_active_window_payload();
    // Insert a previously extracted window payload into this lane.
    void insert_window_payload(const ActiveWindowPayload& payload, Direction direction);
    void set_canvas_geometry(const Box &full_box, const Box &max_box, int gap_size);

    // Remove a window and re-adapt lanes and stacks, returning true on success.
    bool remove_window(PHLWINDOW window);
    bool swapWindows(PHLWINDOW a, PHLWINDOW b);
    void focus_window(PHLWINDOW window);
    FocusMoveResult move_focus(Direction dir, bool focus_wrap);

    // Command-facing stack and window operations.
    void resize_active_stack(int step);
    void resize_active_window(const Vector2D &delta);
    void set_mode(Mode m);
    void align_stack(Direction dir);
    void move_active_stack(Direction dir);
    void admit_window_left();
    void expel_window_right();
    Vector2D predict_window_size() const;
    void update_sizes(PHLMONITOR monitor);
    void set_fullscreen_active_window();
    void toggle_fullscreen_active_window();
    void toggle_maximize_active_stack();
    void fit_size(FitSize fitsize);
    void toggle_overview();
    void recalculate_lane_geometry();

private:
    // Calculate lateral gaps for a stack based on neighbor presence.
    Vector2D calculate_gap_x(const ListNode<Stack *> *stack) const;

    FocusMoveResult move_focus_left(bool focus_wrap);
    FocusMoveResult move_focus_right(bool focus_wrap);
    void move_focus_begin();
    void move_focus_end();

    void center_active_stack();
    void adjust_stacks(ListNode<Stack *> *stack);

    // Raw monitor bounds for this lane's current canvas placement.
    Box full;
    // Workarea bounds after reserved areas and gaps are applied.
    Box max;
    // Whether overview projection is currently active.
    bool overview;
    // Whether this lane is a temporary navigation-only lane.
    bool ephemeral;
    // Inner gap used between stacked windows.
    int gap;
    // Current reorder policy for relayout decisions.
    Reorder reorder;
    // Current navigation/insertion mode for the lane.
    Mode mode;
    // Active stack node inside this lane.
    ListNode<Stack *> *active;
    // Ordered stacks owned by this lane.
    List<Stack *> stacks;
};
