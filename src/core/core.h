/**
 * @file core.h
 * @brief Shared geometry/model utilities used by scroller modules.
 *
 * Centralized helpers for window/monitor adaptation, geometry utilities, and
 * mark storage are kept here so layout and model layers share the same
 * low-level behavior.
 */
#pragma once

#include <string>
#include <unordered_map>

#include <hyprutils/math/Vector2D.hpp>
#include <hyprland/src/helpers/Monitor.hpp>
#include <hyprland/src/layout/target/Target.hpp>
#include <hyprland/src/desktop/view/Window.hpp>

namespace ScrollerCore {

// Lightweight immutable/mutable bbox used for monitor/column geometry.
struct Box {
    Box() : x(0), y(0), w(0), h(0) {}
    Box(double x_, double y_, double w_, double h_)
        : x(x_), y(y_), w(w_), h(h_) {}
    Box(Vector2D pos, Vector2D size)
        : x(pos.x), y(pos.y), w(size.x), h(size.y) {}
    Box(const Box &box)
        : x(box.x), y(box.y), w(box.w), h(box.h) {}

    void set_size(double w_, double h_) {
        w = w_;
        h = h_;
    }
    void set_pos(double x_, double y_) {
        x = x_;
        y = y_;
    }

    double x, y, w, h;
};

// Convert the generic tiled target used by Hyprland into a concrete window pointer.
PHLWINDOW windowFromTarget(SP<Layout::ITarget> target);

// Resolve workspace/monitor context from pointer position used by keyboard actions.
PHLMONITOR monitorFromPointingOrCursor();

// Keep logical and animated window position/size in sync.
void setWindowGeomImmediate(PHLWINDOW window, const Vector2D& pos, const Vector2D& size);

// Update only window position and push animated target when available.
void setWindowPos(PHLWINDOW window, const Vector2D& pos);

// Update only window size and push animated target when available.
void setWindowSize(PHLWINDOW window, const Vector2D& size);

// Read current animated window position.
Vector2D realWindowPosition(PHLWINDOW window);

// Read current animated window size.
Vector2D realWindowSize(PHLWINDOW window);

// Read target window position animation goal.
Vector2D goalWindowPosition(PHLWINDOW window);

// Read target window size animation goal.
Vector2D goalWindowSize(PHLWINDOW window);

// Cross-workspace bookmark table used by marks:* dispatchers.
class Marks {
public:
    Marks() {}
    ~Marks() { reset(); }

    // Remove all marks.
    void reset();
    // Add/replace a mark for the given name.
    void add(PHLWINDOW window, const std::string &name);
    // Delete a mark by name.
    void del(const std::string &name);
    // Remove all marks that refer to a dying window.
    void remove(PHLWINDOW window);
    // Resolve a name to window pointer, or null if missing.
    PHLWINDOW visit(const std::string &name);

private:
    std::unordered_map<std::string, PHLWINDOWREF> marks;
};

} // namespace ScrollerCore
