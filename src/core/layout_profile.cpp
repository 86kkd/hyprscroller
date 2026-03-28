/**
 * @file layout_profile.cpp
 * @brief Shared landscape/portrait behavior helpers for scroller modes.
 */
#include "layout_profile.h"

namespace ScrollerCore {

// Monitor shape decides which orientation semantics the rest of the layout
// helpers should use.
LayoutOrientation layout_orientation_for_extent(double width, double height) {
    return width >= height ? LayoutOrientation::Landscape : LayoutOrientation::Portrait;
}

// Row mode behaves like landscape; column mode behaves like portrait.
LayoutOrientation layout_orientation_for_mode(Mode mode) {
    return mode == Mode::Row ? LayoutOrientation::Landscape : LayoutOrientation::Portrait;
}

// Keep orientation names centralized so debug logs use the same wording.
std::string_view layout_orientation_name(LayoutOrientation orientation) {
    switch (orientation) {
        case LayoutOrientation::Portrait:
            return "portrait";
        case LayoutOrientation::Landscape:
        default:
            return "landscape";
    }
}

// New lanes default to row mode on wide monitors and column mode on tall ones.
Mode default_mode_for_extent(double width, double height) {
    return layout_orientation_for_extent(width, height) == LayoutOrientation::Landscape ? Mode::Row : Mode::Column;
}

// Landscape/row mode fullscreen acts on the whole stack.
bool mode_uses_stack_fullscreen(Mode mode) {
    return mode == Mode::Row;
}

// Portrait/column mode fullscreen is implemented as active-window expansion.
bool mode_uses_window_expansion(Mode mode) {
    return mode == Mode::Column;
}

// Portrait/column mode keeps adding windows into the active vertical stack.
bool mode_adds_windows_into_active_stack(Mode mode) {
    return mode == Mode::Column;
}

// Row mode treats lanes as vertical pages; column mode treats them as horizontal pages.
bool mode_pages_lanes_vertically(Mode mode) {
    return mode == Mode::Row;
}

// Local traversal means left/right in row mode and up/down in column mode.
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

// Classify whether a dispatcher direction should stay within the current lane.
bool direction_targets_local_item(Mode mode, Direction direction) {
    return direction == local_item_backward_direction(mode) ||
           direction == local_item_forward_direction(mode);
}

// Classify whether a dispatcher direction should cross into another lane.
bool direction_moves_between_lanes(Mode mode, Direction direction) {
    return direction == lane_backward_direction(mode) ||
           direction == lane_forward_direction(mode);
}

// "Begin" is treated as an insertion before the current lane in both modes.
bool direction_inserts_before_current(Mode mode, Direction direction) {
    return direction == lane_backward_direction(mode) || direction == Direction::Begin;
}

// Column mode starts windows full-width and half-height; row mode starts them
// half-width and full-height.
Hyprutils::Math::Vector2D predict_window_size(Mode mode, const Box& bounds) {
    if (mode_adds_windows_into_active_stack(mode))
        return {bounds.w, 0.5 * bounds.h};

    return {0.5 * bounds.w, bounds.h};
}

} // namespace ScrollerCore
