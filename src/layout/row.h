/**
 * @file row.h
 * @brief Row-level workspace controller for scroller layout.
 *
 * `Row` owns ordered columns for a single workspace and handles row-level
 * focus movement, command dispatch behavior, fullscreen/maximize transitions,
 * overview mode and geometry updates.
 */
#pragma once

#include "../core/core.h"
#include "../model/model.h"
#include "scroller_layout.h"

using namespace ScrollerCore;
using namespace ScrollerModel;

class Row {
    // A row contains all columns for one workspace and owns horizontal navigation.
public:
    Row(PHLWINDOW window);
    ~Row();

    int get_workspace() const;
    bool has_window(PHLWINDOW window) const;
    PHLWINDOW get_active_window() const;
    bool is_active(PHLWINDOW window) const;
    void add_active_window(PHLWINDOW window);

    // Remove a window and re-adapt rows and columns, returning true on success.
    bool remove_window(PHLWINDOW window);
    bool swapWindows(PHLWINDOW a, PHLWINDOW b);
    void focus_window(PHLWINDOW window);
    bool move_focus(Direction dir, bool focus_wrap);

    void resize_active_column(int step);
    void resize_active_window(const Vector2D &delta);
    void set_mode(Mode m);
    void align_column(Direction dir);
    void move_active_column(Direction dir);
    void admit_window_left();
    void expel_window_right();
    Vector2D predict_window_size() const;
    void update_sizes(PHLMONITOR monitor);
    void set_fullscreen_active_window();
    void toggle_fullscreen_active_window();
    void toggle_maximize_active_column();
    void fit_size(FitSize fitsize);
    void toggle_overview();
    void recalculate_row_geometry();

private:
    // Calculate lateral gaps for a column.
    Vector2D calculate_gap_x(const ListNode<Column *> *column) const;

    bool move_focus_left(bool focus_wrap);
    bool move_focus_right(bool focus_wrap);
    void move_focus_begin();
    void move_focus_end();

    void center_active_column();
    void adjust_columns(ListNode<Column *> *column);

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
