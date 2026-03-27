/**
 * @file layout_profile.cpp
 * @brief Shared landscape/portrait behavior helpers for scroller modes.
 */
#include "layout_profile.h"

namespace ScrollerCore {

LayoutOrientation layout_orientation_for_extent(double width, double height) {
    return width >= height ? LayoutOrientation::Landscape : LayoutOrientation::Portrait;
}

LayoutOrientation layout_orientation_for_mode(Mode mode) {
    return mode == Mode::Row ? LayoutOrientation::Landscape : LayoutOrientation::Portrait;
}

std::string_view layout_orientation_name(LayoutOrientation orientation) {
    switch (orientation) {
        case LayoutOrientation::Portrait:
            return "portrait";
        case LayoutOrientation::Landscape:
        default:
            return "landscape";
    }
}

Mode default_mode_for_extent(double width, double height) {
    return layout_orientation_for_extent(width, height) == LayoutOrientation::Landscape ? Mode::Row : Mode::Column;
}

bool mode_uses_stack_fullscreen(Mode mode) {
    return mode == Mode::Row;
}

bool mode_uses_window_expansion(Mode mode) {
    return mode == Mode::Column;
}

bool mode_adds_windows_into_active_stack(Mode mode) {
    return mode == Mode::Column;
}

bool mode_pages_lanes_vertically(Mode mode) {
    return mode == Mode::Row;
}

Direction local_item_backward_direction(Mode mode) {
    return mode == Mode::Row ? Direction::Left : Direction::Up;
}

Direction local_item_forward_direction(Mode mode) {
    return mode == Mode::Row ? Direction::Right : Direction::Down;
}

Direction lane_backward_direction(Mode mode) {
    return mode == Mode::Row ? Direction::Up : Direction::Left;
}

Direction lane_forward_direction(Mode mode) {
    return mode == Mode::Row ? Direction::Down : Direction::Right;
}

bool direction_targets_local_item(Mode mode, Direction direction) {
    return direction == local_item_backward_direction(mode) ||
           direction == local_item_forward_direction(mode);
}

bool direction_moves_between_lanes(Mode mode, Direction direction) {
    return direction == lane_backward_direction(mode) ||
           direction == lane_forward_direction(mode);
}

bool direction_inserts_before_current(Mode mode, Direction direction) {
    return direction == lane_backward_direction(mode) || direction == Direction::Begin;
}

Hyprutils::Math::Vector2D predict_window_size(Mode mode, const Box& bounds) {
    if (mode_adds_windows_into_active_stack(mode))
        return {bounds.w, 0.5 * bounds.h};

    return {0.5 * bounds.w, bounds.h};
}

} // namespace ScrollerCore
