/**
 * @file layout_profile.h
 * @brief Shared landscape/portrait behavior helpers for scroller modes.
 */
#pragma once

#include <string_view>

#include "types.h"

namespace ScrollerCore {

enum class LayoutOrientation {
    Landscape,
    Portrait,
};

LayoutOrientation            layout_orientation_for_extent(double width, double height);
LayoutOrientation            layout_orientation_for_mode(Mode mode);
std::string_view             layout_orientation_name(LayoutOrientation orientation);
Mode                         default_mode_for_extent(double width, double height);
bool                         mode_uses_stack_fullscreen(Mode mode);
bool                         mode_uses_window_expansion(Mode mode);
bool                         mode_adds_windows_into_active_stack(Mode mode);
bool                         mode_pages_lanes_vertically(Mode mode);
Direction                    local_item_backward_direction(Mode mode);
Direction                    local_item_forward_direction(Mode mode);
Direction                    lane_backward_direction(Mode mode);
Direction                    lane_forward_direction(Mode mode);
bool                         direction_targets_local_item(Mode mode, Direction direction);
bool                         direction_moves_between_lanes(Mode mode, Direction direction);
bool                         direction_inserts_before_current(Mode mode, Direction direction);
Hyprutils::Math::Vector2D    predict_window_size(Mode mode, const Box& bounds);

} // namespace ScrollerCore
