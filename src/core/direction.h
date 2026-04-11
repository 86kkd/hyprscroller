#pragma once

#include <optional>
#include <string_view>

#include "types.h"

namespace ScrollerCore {

// Human-readable direction label used in logs and tests.
const char *direction_name(Direction direction);

// Single-character dispatcher argument used by Hyprland directional commands.
const char *direction_dispatch_arg(Direction direction);

// Return the opposite logical direction when one exists.
Direction opposite_direction(Direction direction);

// Parse direction-like dispatcher arguments into a logical direction.
std::optional<Direction> parse_direction_arg(std::string_view arg);

// Parse fitsize dispatcher arguments into a logical fit mode.
std::optional<FitSize> parse_fit_size_arg(std::string_view arg);

// Parse setmode dispatcher arguments into a layout mode.
std::optional<Mode> parse_mode_arg(std::string_view arg);

} // namespace ScrollerCore
