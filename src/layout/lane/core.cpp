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

Lane::~Lane() {
    for (auto col = stacks.first(); col != nullptr; col = col->next()) {
        delete col->data();
    }
    stacks.clear();
}

bool Lane::has_window(PHLWINDOW window) const {
    for (auto col = stacks.first(); col != nullptr; col = col->next()) {
        if (col->data()->has_window(window))
            return true;
    }
    return false;
}

PHLWINDOW Lane::get_active_window() const {
    return active->data()->get_active_window();
}

bool Lane::is_active(PHLWINDOW window) const {
    return get_active_window() == window;
}
