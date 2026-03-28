/**
 * @file layout_profile.cpp
 * @brief Centralized policy table for row/column layout behavior.
 *
 * The code in this file deliberately stays small and declarative. It answers
 * "which strategy should be used?" questions, while lane/stack/canvas code is
 * responsible for actually mutating geometry.
 */
#include "layout_profile.h"

namespace ScrollerCore {

// A monitor is treated as landscape when width dominates height; otherwise it
// uses portrait semantics.
LayoutOrientation layout_orientation_for_extent(double width, double height) {
    return width >= height ? LayoutOrientation::Landscape : LayoutOrientation::Portrait;
}

// Runtime mode is just another spelling of the same policy choice: row means
// landscape-style behavior, column means portrait-style behavior.
LayoutOrientation layout_orientation_for_mode(Mode mode) {
    return mode == Mode::Row ? LayoutOrientation::Landscape : LayoutOrientation::Portrait;
}

// Keep human-readable orientation names in one place so logs and debug output
// do not drift between modules.
std::string_view layout_orientation_name(LayoutOrientation orientation) {
    switch (orientation) {
        case LayoutOrientation::Portrait:
            return "portrait";
        case LayoutOrientation::Landscape:
        default:
            return "landscape";
    }
}

// New lanes inherit their default mode directly from the monitor shape.
Mode default_mode_for_extent(double width, double height) {
    return layout_orientation_for_extent(width, height) == LayoutOrientation::Landscape ? Mode::Row : Mode::Column;
}

// Row mode treats lanes as a vertical sequence of pages; column mode treats
// lanes as a horizontal sequence.
bool mode_pages_lanes_vertically(Mode mode) {
    return mode == Mode::Row;
}

// "Backward inside the lane" is left in row mode and up in column mode.
Direction local_item_backward_direction(Mode mode) {
    return mode == Mode::Row ? Direction::Left : Direction::Up;
}

// "Forward inside the lane" is right in row mode and down in column mode.
Direction local_item_forward_direction(Mode mode) {
    return mode == Mode::Row ? Direction::Right : Direction::Down;
}

// Stack-local window movement stays on the axis orthogonal to the lane's
// stack axis: up/down in row mode, left/right in column mode.
Direction stack_item_backward_direction(Mode mode) {
    return mode == Mode::Row ? Direction::Up : Direction::Left;
}

Direction stack_item_forward_direction(Mode mode) {
    return mode == Mode::Row ? Direction::Down : Direction::Right;
}

// Moving to the previous lane is vertical in row mode and horizontal in column
// mode because the lane axis flips with the orientation profile.
Direction lane_backward_direction(Mode mode) {
    return mode == Mode::Row ? Direction::Up : Direction::Left;
}

// Moving to the next lane follows the opposite lane axis direction.
Direction lane_forward_direction(Mode mode) {
    return mode == Mode::Row ? Direction::Down : Direction::Right;
}

// Dispatcher code uses this helper to decide whether a direction should stay
// within the current lane instead of attempting a lane-to-lane move.
bool direction_targets_local_item(Mode mode, Direction direction) {
    return direction == local_item_backward_direction(mode) ||
           direction == local_item_forward_direction(mode);
}

// Dispatcher code uses this helper to decide whether a direction should cross
// into another lane/page.
bool direction_moves_between_lanes(Mode mode, Direction direction) {
    return direction == lane_backward_direction(mode) ||
           direction == lane_forward_direction(mode);
}

// Inserts to the "backward lane direction" or explicit "begin" both mean the
// new lane should be placed before the current one.
bool direction_inserts_before_current(Mode mode, Direction direction) {
    return direction == lane_backward_direction(mode) || direction == Direction::Begin;
}

// Predict the initial logical size for a just-created tiled window. Row mode
// starts as a half-width peer stack; column mode starts as a full-width,
// half-height peer stack.
Hyprutils::Math::Vector2D predict_window_size(Mode mode, const Box& bounds) {
    if (mode == Mode::Column)
        return {bounds.w, 0.5 * bounds.h};

    return {0.5 * bounds.w, bounds.h};
}

} // namespace ScrollerCore
