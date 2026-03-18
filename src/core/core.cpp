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

// Keep current and animated window geometry consistent.
void setWindowGeomImmediate(PHLWINDOW window, const Vector2D& pos, const Vector2D& size) {
    if (!window)
        return;

    window->m_position = pos;
    window->m_size     = size;

    if (window->m_realPosition)
        *window->m_realPosition = pos;
    if (window->m_realSize)
        *window->m_realSize = size;
}

// Set only geometry position and animation goal holder when valid.
void setWindowPos(PHLWINDOW window, const Vector2D& pos) {
    if (!window)
        return;

    window->m_position = pos;
    if (window->m_realPosition)
        *window->m_realPosition = pos;
}

// Set only geometry size and animation goal holder when valid.
void setWindowSize(PHLWINDOW window, const Vector2D& size) {
    if (!window)
        return;

    window->m_size = size;
    if (window->m_realSize)
        *window->m_realSize = size;
}

// Read currently committed position, falling back to logical position.
Vector2D realWindowPosition(PHLWINDOW window) {
    if (!window)
        return {};

    if (window->m_realPosition)
        return window->m_realPosition->value();
    return window->m_position;
}

// Read currently committed size, falling back to logical size.
Vector2D realWindowSize(PHLWINDOW window) {
    if (!window)
        return {};

    if (window->m_realSize)
        return window->m_realSize->value();
    return window->m_size;
}

// Read animation goal for position when available.
Vector2D goalWindowPosition(PHLWINDOW window) {
    if (!window)
        return {};

    if (window->m_realPosition)
        return window->m_realPosition->goal();
    return window->m_position;
}

// Read animation goal for size when available.
Vector2D goalWindowSize(PHLWINDOW window) {
    if (!window)
        return {};

    if (window->m_realSize)
        return window->m_realSize->goal();
    return window->m_size;
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
