#include "row.h"

#include <hyprland/src/Compositor.hpp>

Row::Row(PHLWINDOW window)
    : workspace(window->workspaceID()), mode(Mode::Row), reorder(Reorder::Auto),
      overview(false), active(nullptr) {
    const auto monitor = g_pCompositor->getMonitorFromID(window->monitorID());
    if (!monitor)
        return;

    mode = monitor->m_size.x >= monitor->m_size.y ? Mode::Row : Mode::Column;
    update_sizes(monitor);
}

Row::~Row() {
    for (auto col = columns.first(); col != nullptr; col = col->next()) {
        delete col->data();
    }
    columns.clear();
}

int Row::get_workspace() const {
    return workspace;
}

bool Row::has_window(PHLWINDOW window) const {
    for (auto col = columns.first(); col != nullptr; col = col->next()) {
        if (col->data()->has_window(window))
            return true;
    }
    return false;
}

PHLWINDOW Row::get_active_window() const {
    return active->data()->get_active_window();
}

bool Row::is_active(PHLWINDOW window) const {
    return get_active_window() == window;
}
