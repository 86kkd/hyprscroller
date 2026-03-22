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

class Lane {
    // A lane contains all stacks for one workspace and owns horizontal navigation.
public:
    Lane(PHLWINDOW window);
    Lane(PHLMONITOR monitor, Mode mode);
    Lane(Stack *stack);
    ~Lane();

    bool empty() const;
    Mode get_mode() const;
    bool has_window(PHLWINDOW window) const;
    PHLWINDOW get_active_window() const;
    bool is_active(PHLWINDOW window) const;
    void add_active_window(PHLWINDOW window);
    Stack *extract_active_stack();
    void set_canvas_geometry(const Box &full_box, const Box &max_box, int gap_size);

    // Remove a window and re-adapt lanes and stacks, returning true on success.
    bool remove_window(PHLWINDOW window);
    bool swapWindows(PHLWINDOW a, PHLWINDOW b);
    void focus_window(PHLWINDOW window);
    FocusMoveResult move_focus(Direction dir, bool focus_wrap);

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
    // Calculate lateral gaps for a stack.
    Vector2D calculate_gap_x(const ListNode<Stack *> *stack) const;

    FocusMoveResult move_focus_left(bool focus_wrap);
    FocusMoveResult move_focus_right(bool focus_wrap);
    void move_focus_begin();
    void move_focus_end();

    void center_active_stack();
    void adjust_stacks(ListNode<Stack *> *stack);

    Box full;
    Box max;
    bool overview;
    int gap;
    Reorder reorder;
    Mode mode;
    ListNode<Stack *> *active;
    List<Stack *> stacks;
};
