/**
 * @file monitor_geometry.h
 * @brief Helpers for monitor bounds in logical (transform-aware) space.
 */
#pragma once

#include <wayland-server-protocol.h>
#include <hyprutils/math/Vector2D.hpp>

#include "types.h"

namespace ScrollerCore {

struct ReservedEdges {
    double top = 0.0;
    double right = 0.0;
    double bottom = 0.0;
    double left = 0.0;
};

// Return the monitor size in compositor logical space after output transform.
Hyprutils::Math::Vector2D logical_monitor_size(const Hyprutils::Math::Vector2D& rawSize,
                                               const Hyprutils::Math::Vector2D& transformedSize,
                                               wl_output_transform transform);
// Return the monitor rectangle in compositor logical space after output transform.
Box                       logical_monitor_box(const Hyprutils::Math::Vector2D& position,
                                             const Hyprutils::Math::Vector2D& rawSize,
                                             const Hyprutils::Math::Vector2D& transformedSize,
                                             wl_output_transform transform);
// Return reserved edges mapped into compositor logical space.
ReservedEdges             logical_reserved_edges(const Hyprutils::Math::Vector2D& rawSize,
                                                const Hyprutils::Math::Vector2D& transformedSize,
                                                wl_output_transform transform,
                                                const ReservedEdges& rawReserved);
// Return the usable monitor workarea in compositor logical space.
Box                       logical_workarea_box(const Hyprutils::Math::Vector2D& position,
                                               const Hyprutils::Math::Vector2D& rawSize,
                                               const Hyprutils::Math::Vector2D& transformedSize,
                                               wl_output_transform transform,
                                               const ReservedEdges& rawReserved,
                                               double outerGap);

} // namespace ScrollerCore
