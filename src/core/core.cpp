#include "core.h"

#include <hyprland/src/Compositor.hpp>

namespace ScrollerCore {

PHLWINDOW windowFromTarget(SP<Layout::ITarget> target) {
    return target ? target->window() : nullptr;
}

PHLMONITOR monitorFromPointingOrCursor() {
    if (auto monitor = g_pCompositor->getMonitorFromCursor(); monitor)
        return monitor;
    return nullptr;
}

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

void setWindowPos(PHLWINDOW window, const Vector2D& pos) {
    if (!window)
        return;

    window->m_position = pos;
    if (window->m_realPosition)
        *window->m_realPosition = pos;
}

void setWindowSize(PHLWINDOW window, const Vector2D& size) {
    if (!window)
        return;

    window->m_size = size;
    if (window->m_realSize)
        *window->m_realSize = size;
}

Vector2D realWindowPosition(PHLWINDOW window) {
    if (!window)
        return {};

    if (window->m_realPosition)
        return window->m_realPosition->value();
    return window->m_position;
}

Vector2D realWindowSize(PHLWINDOW window) {
    if (!window)
        return {};

    if (window->m_realSize)
        return window->m_realSize->value();
    return window->m_size;
}

Vector2D goalWindowPosition(PHLWINDOW window) {
    if (!window)
        return {};

    if (window->m_realPosition)
        return window->m_realPosition->goal();
    return window->m_position;
}

Vector2D goalWindowSize(PHLWINDOW window) {
    if (!window)
        return {};

    if (window->m_realSize)
        return window->m_realSize->goal();
    return window->m_size;
}

void Marks::reset() {
    marks.clear();
}

void Marks::add(PHLWINDOW window, const std::string &name) {
    const auto mark = marks.find(name);
    if (mark != marks.end()) {
        mark->second = window;
        return;
    }
    marks[name] = window;
}

void Marks::del(const std::string &name) {
    const auto mark = marks.find(name);
    if (mark != marks.end()) {
        marks.erase(mark);
    }
}

void Marks::remove(PHLWINDOW window) {
    for(auto it = marks.begin(); it != marks.end();) {
        if (it->second.lock() == window)
            it = marks.erase(it);
        else
            it++;
    }
}

PHLWINDOW Marks::visit(const std::string &name) {
    const auto mark = marks.find(name);
    if (mark != marks.end()) {
        return mark->second.lock();
    }
    return nullptr;
}

} // namespace ScrollerCore
