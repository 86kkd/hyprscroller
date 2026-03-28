/**
 * @file layout_profile.h
 * @brief Shared landscape/portrait behavior helpers for scroller modes.
 */
#pragma once

#include <string_view>

#include "types.h"

namespace ScrollerCore {

// Shared orientation vocabulary used by the mode helpers below. In this
// plugin, landscape semantics map to row mode, while portrait semantics map to
// column mode.
enum class LayoutOrientation {
    // Wide monitor semantics: row mode, horizontal local traversal, stack-wide
    // fullscreen.
    Landscape,
    // Tall monitor semantics: column mode, vertical local traversal,
    // per-window expansion instead of stack-wide fullscreen.
    Portrait,
};

// Infer the semantic layout orientation from monitor dimensions.
LayoutOrientation            layout_orientation_for_extent(double width, double height);
// Map the runtime lane mode to the orientation semantics used by helpers.
LayoutOrientation            layout_orientation_for_mode(Mode mode);
// Stable string name for logs/debugging.
std::string_view             layout_orientation_name(LayoutOrientation orientation);
// Choose the default scroller mode for a monitor shape.
Mode                         default_mode_for_extent(double width, double height);
// Row mode expands the whole stack horizontally like fullscreen.
bool                         mode_uses_stack_fullscreen(Mode mode);
// Column mode expands only the active window vertically.
bool                         mode_uses_window_expansion(Mode mode);
// Column mode inserts new windows into the current active stack.
bool                         mode_adds_windows_into_active_stack(Mode mode);
// Row mode pages lanes along the vertical axis; column mode pages them sideways.
bool                         mode_pages_lanes_vertically(Mode mode);
// Direction that moves to the previous local item inside the current lane.
Direction                    local_item_backward_direction(Mode mode);
// Direction that moves to the next local item inside the current lane.
Direction                    local_item_forward_direction(Mode mode);
// Direction that moves to the previous lane.
Direction                    lane_backward_direction(Mode mode);
// Direction that moves to the next lane.
Direction                    lane_forward_direction(Mode mode);
// Return true when a direction should be handled as an in-lane item movement.
bool                         direction_targets_local_item(Mode mode, Direction direction);
// Return true when a direction should move across lanes.
bool                         direction_moves_between_lanes(Mode mode, Direction direction);
// Return true when an insertion direction means "place before current lane".
bool                         direction_inserts_before_current(Mode mode, Direction direction);
// Predict the initial logical size of a newly created window for this mode.
Hyprutils::Math::Vector2D    predict_window_size(Mode mode, const Box& bounds);

} // namespace ScrollerCore
