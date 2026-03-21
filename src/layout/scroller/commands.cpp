#include "../row/row.h"
#include "layout.h"
#include "internal.h"

void ScrollerLayout::cycle_window_size(int workspace, int step)
{
    auto row = getRowForWorkspace(workspace);
    if (!row)
        return;

    row->resize_active_column(step);
}

void ScrollerLayout::move_window(int workspace, Direction direction) {
    auto row = getRowForWorkspace(workspace);
    if (!row)
        return;

    row->move_active_column(direction);
    ScrollerLayoutInternal::switch_to_window(row->get_active_window());
}

void ScrollerLayout::align_window(int workspace, Direction direction) {
    auto row = getRowForWorkspace(workspace);
    if (!row)
        return;

    row->align_column(direction);
}

void ScrollerLayout::admit_window_left(int workspace) {
    auto row = getRowForWorkspace(workspace);
    if (!row)
        return;

    row->admit_window_left();
}

void ScrollerLayout::expel_window_right(int workspace) {
    auto row = getRowForWorkspace(workspace);
    if (!row)
        return;

    row->expel_window_right();
}

void ScrollerLayout::set_mode(int workspace, Mode mode) {
    auto row = getRowForWorkspace(workspace);
    if (!row)
        return;

    row->set_mode(mode);
}

void ScrollerLayout::fit_size(int workspace, FitSize fitsize) {
    auto row = getRowForWorkspace(workspace);
    if (!row)
        return;

    row->fit_size(fitsize);
}

void ScrollerLayout::toggle_overview(int workspace) {
    auto row = getRowForWorkspace(workspace);
    if (!row)
        return;

    row->toggle_overview();
}

void ScrollerLayout::toggle_fullscreen(int workspace) {
    auto row = getRowForWorkspace(workspace);
    if (!row)
        return;

    row->toggle_fullscreen_active_window();
}
