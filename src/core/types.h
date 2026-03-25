#pragma once

#include <hyprutils/math/Vector2D.hpp>

enum class Direction { Left, Right, Up, Down, Begin, End, Center };
enum class FitSize { Active, Visible, All, ToEnd, ToBeg };
enum class Mode { Row, Column };
enum class FocusMoveResult { Moved, NoOp, CrossMonitor };

namespace ScrollerCore {

// Lightweight bbox shared by pure geometry helpers and layout code.
struct Box {
    Box() = default;
    Box(double x_, double y_, double w_, double h_)
        : x(x_), y(y_), w(w_), h(h_) {}
    Box(Hyprutils::Math::Vector2D pos, Hyprutils::Math::Vector2D size)
        : x(pos.x), y(pos.y), w(size.x), h(size.y) {}
    Box(const Box &) = default;
    Box &operator=(const Box &) = default;

    void set_size(double w_, double h_) {
        w = w_;
        h = h_;
    }

    void set_pos(double x_, double y_) {
        x = x_;
        y = y_;
    }

    double x = 0.0;
    double y = 0.0;
    double w = 0.0;
    double h = 0.0;
};

} // namespace ScrollerCore
