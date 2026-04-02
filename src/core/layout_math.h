#pragma once

#include <span>

#include "types.h"

namespace ScrollerCore {

// Minimal geometry input needed to compute overview projection bounds.
struct OverviewRect {
    double x0 = 0.0;
    double x1 = 0.0;
    double y0 = 0.0;
    double y1 = 0.0;
};

// Pure overview projection result shared by layout code and tests.
struct OverviewProjection {
    Hyprutils::Math::Vector2D min = {};
    Hyprutils::Math::Vector2D max = {};
    double   width = 0.0;
    double   height = 0.0;
    double   scale = 1.0;
    Hyprutils::Math::Vector2D offset = {};
};

// Choose the horizontal anchor that keeps the active stack and a useful neighbor visible.
double choose_anchor_x(bool has_next, bool has_prev, double active_width, double next_width,
                       double prev_width, double fallback_x, const Box &visible_box);

// Choose the vertical anchor that keeps the active window and a useful neighbor visible.
double choose_anchor_y(bool has_next, bool has_prev, double active_height, double next_height,
                       double prev_height, const Box &visible_box);

// Center an inner span inside an outer span while preserving the outer origin.
double center_span(double origin, double outer_span, double inner_span);

// Compute the overview projection for a set of stack bounds inside the visible box.
OverviewProjection compute_overview_projection(std::span<const OverviewRect> items,
                                               const Box &visible_box);

} // namespace ScrollerCore
