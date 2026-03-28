/**
 * @file layout_profile.h
 * @brief Orientation policy helpers shared by lane, stack, and canvas code.
 *
 * This module is the single place that explains what "landscape" and
 * "portrait" mean inside hyprscroller. The rest of the layout code should ask
 * these helpers questions such as:
 *
 * - Should this monitor default to row mode or column mode?
 * - Does fullscreen mean "expand the whole stack" or "stretch only the active
 *   window"?
 * - Does a direction stay inside the current lane, or should it cross into a
 *   neighboring lane?
 * - Should a new window join the current stack, or start a new peer stack?
 *
 * In practice the policy is:
 *
 * - Landscape semantics map to `Mode::Row`
 * - Portrait semantics map to `Mode::Column`
 */
#pragma once

#include <string_view>

#include "types.h"

namespace ScrollerCore {

/**
 * Semantic orientation profile used by the helpers below.
 *
 * This is not just the physical monitor shape. It also describes which layout
 * behavior family to use.
 */
enum class LayoutOrientation {
    /**
     * Wide-monitor / row-mode profile.
     *
     * - Local movement is left/right
     * - Lane movement is up/down
     * - New windows typically become new peer stacks
     * - Fullscreen expands the active stack as a whole
     */
    Landscape,

    /**
     * Tall-monitor / column-mode profile.
     *
     * - Local movement is up/down
     * - Lane movement is left/right
     * - New windows still create peer stacks by default
     * - Fullscreen stretches only the active window; sibling windows still
     *   belong to the same stack and can be restored afterward
     */
    Portrait,
};

// Convert raw monitor dimensions into the semantic orientation profile.
LayoutOrientation         layout_orientation_for_extent(double width, double height);

// Convert an already chosen runtime mode into the matching orientation profile.
LayoutOrientation         layout_orientation_for_mode(Mode mode);

// Small stable name used by logs, traces, and debug output.
std::string_view          layout_orientation_name(LayoutOrientation orientation);

// Pick the default lane mode for a monitor shape: row on wide monitors,
// column on tall monitors.
Mode                      default_mode_for_extent(double width, double height);

// Return true when fullscreen should resize the owning stack itself. This is
// the landscape/row-mode behavior.
bool                      mode_uses_stack_fullscreen(Mode mode);

// Return true when fullscreen should only stretch the active window inside its
// current stack. This is the portrait/column-mode behavior.
bool                      mode_uses_window_expansion(Mode mode);

// Return true when a newly created window should be inserted into the current
// active stack instead of creating a new peer stack. This is now false for the
// built-in row and column policies; stack merges happen through explicit move
// commands instead of ordinary window creation.
bool                      mode_adds_windows_into_active_stack(Mode mode);

// Return true when moving between lanes/pages should happen on the vertical
// axis rather than the horizontal axis.
bool                      mode_pages_lanes_vertically(Mode mode);

// Direction that means "previous local item inside the current lane".
Direction                 local_item_backward_direction(Mode mode);

// Direction that means "next local item inside the current lane".
Direction                 local_item_forward_direction(Mode mode);

// Direction that means "previous window inside the current stack".
Direction                 stack_item_backward_direction(Mode mode);

// Direction that means "next window inside the current stack".
Direction                 stack_item_forward_direction(Mode mode);

// Direction that means "previous lane".
Direction                 lane_backward_direction(Mode mode);

// Direction that means "next lane".
Direction                 lane_forward_direction(Mode mode);

// Return true when a direction should be handled as movement inside the
// current lane rather than a lane-to-lane transition.
bool                      direction_targets_local_item(Mode mode, Direction direction);

// Return true when a direction should cross into another lane/page.
bool                      direction_moves_between_lanes(Mode mode, Direction direction);

// Return true when an insertion request means "place before the current lane".
bool                      direction_inserts_before_current(Mode mode, Direction direction);

// Predict the starting logical size for a new tiled window under this mode.
Hyprutils::Math::Vector2D predict_window_size(Mode mode, const Box& bounds);

} // namespace ScrollerCore
