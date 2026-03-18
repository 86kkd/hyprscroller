#pragma once

#include <string>
#include <unordered_map>

#include <hyprutils/math/Vector2D.hpp>
#include <hyprland/src/helpers/Monitor.hpp>
#include <hyprland/src/layout/target/Target.hpp>
#include <hyprland/src/desktop/view/Window.hpp>

namespace ScrollerCore {

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

PHLWINDOW windowFromTarget(SP<Layout::ITarget> target);
PHLMONITOR monitorFromPointingOrCursor();

void setWindowGeomImmediate(PHLWINDOW window, const Vector2D& pos, const Vector2D& size);
void setWindowPos(PHLWINDOW window, const Vector2D& pos);
void setWindowSize(PHLWINDOW window, const Vector2D& size);
Vector2D realWindowPosition(PHLWINDOW window);
Vector2D realWindowSize(PHLWINDOW window);
Vector2D goalWindowPosition(PHLWINDOW window);
Vector2D goalWindowSize(PHLWINDOW window);

class Marks {
public:
    Marks() {}
    ~Marks() { reset(); }
    void reset();
    void add(PHLWINDOW window, const std::string &name);
    void del(const std::string &name);
    void remove(PHLWINDOW window);
    PHLWINDOW visit(const std::string &name);

private:
    std::unordered_map<std::string, PHLWINDOWREF> marks;
};

} // namespace ScrollerCore
