#include "../lane/lane.h"
#include "layout.h"
#include "internal.h"

void CanvasLayout::cycle_window_size(int workspace, int step)
{
    auto lane = getLaneForWorkspace(workspace);
    if (!lane)
        return;

    lane->resize_active_stack(step);
}

void CanvasLayout::move_window(int workspace, Direction direction) {
    auto lane = getLaneForWorkspace(workspace);
    if (!lane)
        return;

    lane->move_active_stack(direction);
    CanvasLayoutInternal::switch_to_window(lane->get_active_window());
}

void CanvasLayout::align_window(int workspace, Direction direction) {
    auto lane = getLaneForWorkspace(workspace);
    if (!lane)
        return;

    lane->align_stack(direction);
}

void CanvasLayout::admit_window_left(int workspace) {
    auto lane = getLaneForWorkspace(workspace);
    if (!lane)
        return;

    lane->admit_window_left();
}

void CanvasLayout::expel_window_right(int workspace) {
    auto lane = getLaneForWorkspace(workspace);
    if (!lane)
        return;

    lane->expel_window_right();
}

void CanvasLayout::set_mode(int workspace, Mode mode) {
    auto lane = getLaneForWorkspace(workspace);
    if (!lane)
        return;

    lane->set_mode(mode);
}

void CanvasLayout::fit_size(int workspace, FitSize fitsize) {
    auto lane = getLaneForWorkspace(workspace);
    if (!lane)
        return;

    lane->fit_size(fitsize);
}

void CanvasLayout::toggle_overview(int workspace) {
    auto lane = getLaneForWorkspace(workspace);
    if (!lane)
        return;

    lane->toggle_overview();
}

void CanvasLayout::toggle_fullscreen(int workspace) {
    auto lane = getLaneForWorkspace(workspace);
    if (!lane)
        return;

    lane->toggle_fullscreen_active_window();
}
