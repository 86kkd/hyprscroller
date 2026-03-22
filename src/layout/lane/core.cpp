#include "lane.h"

#include <hyprland/src/Compositor.hpp>

Lane::Lane(PHLWINDOW window)
    : mode(Mode::Row), reorder(Reorder::Auto), overview(false), active(nullptr) {
    const auto monitor = g_pCompositor->getMonitorFromID(window->monitorID());
    if (!monitor)
        return;

    mode = monitor->m_size.x >= monitor->m_size.y ? Mode::Row : Mode::Column;
    update_sizes(monitor);
}

Lane::Lane(PHLMONITOR monitor, Mode laneMode)
    : mode(laneMode), reorder(Reorder::Auto), overview(false), active(nullptr) {
    if (monitor)
        update_sizes(monitor);
}

Lane::Lane(Stack *stack)
    : mode(Mode::Row), reorder(Reorder::Auto), overview(false), active(nullptr) {
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

void Lane::set_canvas_geometry(const Box &full_box, const Box &max_box, int gap_size) {
    full = full_box;
    max = max_box;
    gap = gap_size;
}
