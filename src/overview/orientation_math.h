/**
 * @file orientation_math.h
 * @brief Pure orientation and coordinate-transform helpers.
 */
#pragma once

#include <string_view>

#include <wayland-client-protocol.h>

#include "../core/types.h"

namespace Overview {

enum class MonitorOrientation {
    Landscape,
    Portrait,
};

bool              transform_swaps_axes(wl_output_transform transform);
MonitorOrientation orientation_for_transform(wl_output_transform transform);
std::string_view  orientation_name(MonitorOrientation orientation);
ScrollerCore::Box transform_box_to_render_space(const ScrollerCore::Box& box,
                                                wl_output_transform transform,
                                                double renderWidth,
                                                double renderHeight);

} // namespace Overview
