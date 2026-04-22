/**
 * @file window.cpp
 * @brief Implementation of the lightweight per-window model used by `Stack`.
 *
 * This file intentionally stays small: it only manages logical height and Y
 * state for a single tiled window. Canvas, lane, and stack placement are
 * handled at higher layers.
 */
#include "model/stack/stack.h"

#include "core/layout_profile.h"

namespace ScrollerModel {

Window::Window(PHLWINDOW window, double box_h, Mode mode)
    : window(window),
      height(WindowHeight::One),
      box_y(window ? ScrollerCore::stack_local_origin_for_window(mode, window->m_position) - window->getRealBorderSize() : 0.0),
      box_h(box_h) {}

PHLWINDOWREF Window::ptr() const {
    return window;
}

double Window::get_geom_h() const {
    return box_h;
}

double Window::get_geom_y() const {
    return box_y;
}

void Window::set_geom_h(double geom_h) {
    box_h = geom_h;
}

void Window::set_geom_y(double geom_y) {
    box_y = geom_y;
}

void Window::push_geom() {
    // Save logical geometry before a temporary transform such as expand/overview.
    mem.box_h = box_h;
    mem.box_y = box_y;
}

void Window::pop_geom() {
    // Restore the previously saved logical geometry.
    box_h = mem.box_h;
    box_y = mem.box_y;
}

double Window::get_saved_geom_y() const {
    return mem.box_y;
}

double Window::get_saved_geom_h() const {
    return mem.box_h;
}

WindowHeight Window::get_height() const {
    return height;
}

void Window::update_height(WindowHeight h, double max) {
    height = h;
    switch (height) {
    case WindowHeight::One:
        box_h = max;
        break;
    case WindowHeight::TwoThirds:
        box_h = 2.0 * max / 3.0;
        break;
    case WindowHeight::OneHalf:
        box_h = 0.5 * max;
        break;
    case WindowHeight::OneThird:
        box_h = max / 3.0;
        break;
    default:
        break;
    }
}

void Window::set_height_free() {
    height = WindowHeight::Free;
}

void Window::restore_state(WindowHeight h, double geom_y, double geom_h, double mem_y, double mem_h) {
    height = h;
    box_y = geom_y;
    box_h = geom_h;
    mem.box_y = mem_y;
    mem.box_h = mem_h;
}

ScrollerSnapshot::WindowSnapshot Window::capture_snapshot() const {
    return {
        .key = window ? reinterpret_cast<uintptr_t>(window.get()) : 0,
        .heightMode = static_cast<int>(height),
        .geomY = box_y,
        .geomH = box_h,
        .memY = mem.box_y,
        .memH = mem.box_h,
    };
}

} // namespace ScrollerModel
