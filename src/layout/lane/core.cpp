#include "lane.h"

#include <hyprland/src/Compositor.hpp>

Lane::Lane(PHLWINDOW window)
    : mode(Mode::Row), reorder(Reorder::Auto), overview(false), ephemeral(false), active(nullptr) {
    const auto monitor = g_pCompositor->getMonitorFromID(window->monitorID());
    if (!monitor)
        return;

    mode = monitor->m_size.x >= monitor->m_size.y ? Mode::Row : Mode::Column;
    update_sizes(monitor);
}

Lane::Lane(PHLMONITOR monitor, Mode laneMode)
    : mode(laneMode), reorder(Reorder::Auto), overview(false), ephemeral(false), active(nullptr) {
    if (monitor)
        update_sizes(monitor);
}

Lane::Lane(Stack *stack)
    : mode(Mode::Row), reorder(Reorder::Auto), overview(false), ephemeral(false), active(nullptr) {
    const auto window = stack ? stack->get_active_window() : nullptr;
    const auto monitor = window ? g_pCompositor->getMonitorFromID(window->monitorID()) : nullptr;
    if (monitor) {
        mode = monitor->m_size.x >= monitor->m_size.y ? Mode::Row : Mode::Column;
        update_sizes(monitor);
    }

    if (!stack)
        return;

    stacks.push_back(stack);
    active = stacks.first();
}

Lane::~Lane() {
    for (auto col = stacks.first(); col != nullptr; col = col->next()) {
        delete col->data();
    }
    stacks.clear();
}

bool Lane::empty() const {
    return stacks.empty();
}

Mode Lane::get_mode() const {
    return mode;
}

bool Lane::is_ephemeral() const {
    return ephemeral;
}

void Lane::set_ephemeral(bool value) {
    ephemeral = value;
}

bool Lane::has_window(PHLWINDOW window) const {
    for (auto col = stacks.first(); col != nullptr; col = col->next()) {
        if (col->data()->has_window(window))
            return true;
    }
    return false;
}

PHLWINDOW Lane::get_active_window() const {
    if (!active)
        return nullptr;

    return active->data()->get_active_window();
}

bool Lane::is_active(PHLWINDOW window) const {
    return get_active_window() == window;
}

Stack *Lane::extract_active_stack() {
    if (!active)
        return nullptr;

    auto node = active;
    auto stack = node->data();
    active = node != stacks.last() ? node->next() : node->prev();
    stacks.erase(node);
    return stack;
}

Window *Lane::extract_active_window(StackWidth *width, double *maxw) {
    if (!active)
        return nullptr;

    auto *stack = active->data();
    if (width)
        *width = stack->get_width();
    if (maxw)
        *maxw = stack->get_width() == StackWidth::Free ? stack->get_geom_w() : max.w;

    auto *window = stack->expel_active(gap);
    if (!window)
        return nullptr;

    if (stack->size() != 0) {
        reorder = Reorder::Auto;
        stack->recalculate_stack_geometry(calculate_gap_x(active), gap);
        return window;
    }

    auto emptyNode = active;
    active = emptyNode == stacks.last() ? emptyNode->prev() : emptyNode->next();
    delete stack;
    stacks.erase(emptyNode);
    reorder = Reorder::Auto;
    return window;
}

void Lane::insert_window(Window *window, StackWidth width, double maxw, Direction direction) {
    if (!window)
        return;

    reorder = Reorder::Auto;
    if (mode == Mode::Column && active) {
        active->data()->admit_window(window);
        recalculate_lane_geometry();
        return;
    }

    window->set_geom_h(max.h);
    window->set_geom_y(max.y);
    auto *stack = new Stack(window, width, maxw, max.h);
    stack->set_geom_pos(max.x, max.y);
    if (!active) {
        stacks.push_back(stack);
        active = stacks.last();
        recalculate_lane_geometry();
        return;
    }

    auto current = active;
    auto inserted = stacks.emplace_after(current, stack);
    if (direction == Direction::Left || direction == Direction::Up || direction == Direction::Begin)
        stacks.move_before(current, inserted);
    active = inserted;
    recalculate_lane_geometry();
}

void Lane::set_canvas_geometry(const Box &full_box, const Box &max_box, int gap_size) {
    full = full_box;
    max = max_box;
    gap = gap_size;
}
