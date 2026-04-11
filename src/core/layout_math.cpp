#include "layout_math.h"

#include <algorithm>

namespace ScrollerCore {

double choose_anchor_x(bool has_next, bool has_prev, double active_width, double next_width,
                       double prev_width, double fallback_x, const Box &visible_box) {
    if (has_next) {
        if (active_width + next_width <= visible_box.w)
            return visible_box.x + visible_box.w - active_width - next_width;
        if (has_prev && prev_width + active_width <= visible_box.w)
            return visible_box.x + prev_width;
        if (!has_prev)
            return visible_box.x;
        return fallback_x;
    }

    if (has_prev) {
        if (prev_width + active_width <= visible_box.w)
            return visible_box.x + prev_width;
        return visible_box.x + visible_box.w - active_width;
    }

    return fallback_x;
}

double choose_anchor_y(bool has_next, bool has_prev, double active_height, double next_height,
                       double prev_height, const Box &visible_box) {
    const auto base_y = visible_box.y;
    const auto stack_to_bottom = visible_box.y + visible_box.h - active_height;
    if (has_next && active_height + next_height <= visible_box.h)
        return visible_box.y + visible_box.h - active_height - next_height;
    if (has_next && has_prev && prev_height + active_height <= visible_box.h)
        return visible_box.y + prev_height;
    if (!has_next && has_prev && prev_height + active_height <= visible_box.h)
        return visible_box.y + prev_height;
    if (!has_next && has_prev)
        return stack_to_bottom;
    return base_y;
}

double center_span(double origin, double outer_span, double inner_span) {
    return origin + 0.5 * (outer_span - inner_span);
}

OverviewProjection compute_overview_projection(std::span<const OverviewRect> items,
                                               const Box &visible_box) {
    if (items.empty())
        return {};

    Hyprutils::Math::Vector2D bmin(visible_box.x + visible_box.w, visible_box.y + visible_box.h);
    Hyprutils::Math::Vector2D bmax(visible_box.x, visible_box.y);
    for (const auto &item : items) {
        if (item.x0 < bmin.x)
            bmin.x = item.x0;
        if (item.x1 > bmax.x)
            bmax.x = item.x1;
        if (item.y0 < bmin.y)
            bmin.y = item.y0;
        if (item.y1 > bmax.y)
            bmax.y = item.y1;
    }

    const auto width = bmax.x - bmin.x;
    const auto height = bmax.y - bmin.y;
    if (width <= 0.0 || height <= 0.0) {
        const auto offset = Hyprutils::Math::Vector2D(bmin.x - visible_box.x, bmin.y - visible_box.y);
        return OverviewProjection{bmin, bmax, width, height, 1.0, offset};
    }

    const auto scale = std::min(visible_box.w / width, visible_box.h / height);
    const auto offset = Hyprutils::Math::Vector2D(0.5 * (visible_box.w - width * scale),
                                                  0.5 * (visible_box.h - height * scale));
    return OverviewProjection{bmin, bmax, width, height, scale, offset};
}

} // namespace ScrollerCore
