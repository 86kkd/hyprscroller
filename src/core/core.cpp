#include "core.h"

#include <hyprland/src/Compositor.hpp>

namespace ScrollerCore {

// Return null if target doesn't carry a window yet.
PHLWINDOW windowFromTarget(SP<Layout::ITarget> target) {
    return target ? target->window() : nullptr;
}

// Use cursor monitor first as action context when available.
PHLMONITOR monitorFromPointingOrCursor() {
    if (auto monitor = g_pCompositor->getMonitorFromCursor(); monitor)
        return monitor;
    return nullptr;
}

// Drop all marks to avoid stale references after disable/reset.
void Marks::reset() {
    marks.clear();
}

// Add or replace bookmark name with the provided window.
void Marks::add(PHLWINDOW window, const std::string &name) {
    const auto mark = marks.find(name);
    if (mark != marks.end()) {
        mark->second = window;
        return;
    }
    marks[name] = window;
}

// Delete bookmark by name only.
void Marks::del(const std::string &name) {
    const auto mark = marks.find(name);
    if (mark != marks.end()) {
        marks.erase(mark);
    }
}

// Remove every mark that points at the closed/unmanaged window.
void Marks::remove(PHLWINDOW window) {
    for(auto it = marks.begin(); it != marks.end();) {
        if (it->second.lock() == window)
            it = marks.erase(it);
        else
            it++;
    }
}

// Resolve bookmark name to a live window handle.
PHLWINDOW Marks::visit(const std::string &name) {
    const auto mark = marks.find(name);
    if (mark != marks.end()) {
        return mark->second.lock();
    }
    return nullptr;
}

} // namespace ScrollerCore
