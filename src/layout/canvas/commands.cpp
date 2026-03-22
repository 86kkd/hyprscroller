#include "../lane/lane.h"
#include "layout.h"
#include "internal.h"

void CanvasLayout::cycle_window_size(int workspace, int step)
{
    (void)workspace;
    auto lane = getActiveLane();
    if (!lane)
        return;

    lane->resize_active_stack(step);
}

void CanvasLayout::move_window(int workspace, Direction direction) {
    (void)workspace;
    auto lane = getActiveLane();
    if (!lane)
        return;

    lane->move_active_stack(direction);
    CanvasLayoutInternal::switch_to_window(lane->get_active_window());
}

void CanvasLayout::align_window(int workspace, Direction direction) {
    (void)workspace;
    auto lane = getActiveLane();
    if (!lane)
        return;

    lane->align_stack(direction);
}

void CanvasLayout::admit_window_left(int workspace) {
    (void)workspace;
    auto lane = getActiveLane();
    if (!lane)
        return;

    lane->admit_window_left();
}

void CanvasLayout::expel_window_right(int workspace) {
    (void)workspace;
    auto lane = getActiveLane();
    if (!lane)
        return;

    lane->expel_window_right();
}

void CanvasLayout::set_mode(int workspace, Mode mode) {
    (void)workspace;
    auto lane = getActiveLane();
    if (!lane)
        return;

    lane->set_mode(mode);
}

void CanvasLayout::fit_size(int workspace, FitSize fitsize) {
    (void)workspace;
    auto lane = getActiveLane();
    if (!lane)
        return;

    lane->fit_size(fitsize);
}

void CanvasLayout::toggle_overview(int workspace) {
    (void)workspace;
    auto lane = getActiveLane();
    if (!lane)
        return;

    lane->toggle_overview();
}

void CanvasLayout::toggle_fullscreen(int workspace) {
    (void)workspace;
    syncActiveStateFromWorkspaceFocus();
    auto lane = getActiveLane();
    if (!lane)
        return;

    lane->toggle_fullscreen_active_window();
}

void CanvasLayout::create_lane(int workspace, Direction direction) {
    (void)workspace;
    auto lane = getActiveLane();
    if (!lane || !activeLane)
        return;

    auto stack = lane->extract_active_stack();
    if (!stack)
        return;

    auto currentLaneNode = activeLane;
    auto newLane = new Lane(stack);
    lanes.push_back(newLane);
    auto newLaneNode = lanes.last();

    if (direction == Direction::Left || direction == Direction::Up || direction == Direction::Begin)
        lanes.move_before(currentLaneNode, newLaneNode);
    else if (currentLaneNode != newLaneNode)
        lanes.move_after(currentLaneNode, newLaneNode);

    activeLane = newLaneNode;

    if (lane->empty()) {
        lanes.erase(currentLaneNode);
        delete lane;
    }

    const auto workspaceHandle = getCanvasWorkspace();
    const auto monitor = workspaceHandle ? CanvasLayoutInternal::visible_monitor_for_workspace(workspaceHandle) : nullptr;
    if (monitor)
        relayoutCanvas(monitor, workspaceHandle && !workspaceHandle->m_isSpecialWorkspace);

    if (const auto window = newLane->get_active_window())
        CanvasLayoutInternal::switch_to_window(window, true);
}

void CanvasLayout::focus_lane(int workspace, Direction direction) {
    (void)workspace;
    if (!activeLane || lanes.size() < 2)
        return;

    auto target = activeLane;
    switch (direction) {
    case Direction::Left:
    case Direction::Up:
        target = activeLane->prev() ? activeLane->prev() : lanes.last();
        break;
    case Direction::Right:
    case Direction::Down:
        target = activeLane->next() ? activeLane->next() : lanes.first();
        break;
    case Direction::Begin:
        target = lanes.first();
        break;
    case Direction::End:
        target = lanes.last();
        break;
    default:
        return;
    }

    if (!target || target == activeLane)
        return;

    activeLane = target;
    if (const auto window = activeLane->data()->get_active_window())
        CanvasLayoutInternal::switch_to_window(window, true);
}
