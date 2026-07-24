/**
 * @file monitor_geometry_runtime.h
 * @brief Hyprland monitor wrappers over the pure monitor geometry helpers.
 */
#pragma once

#include <hyprland/src/output/Monitor.hpp>

#include "monitor_geometry.h"

namespace ScrollerCore {

ReservedEdges             raw_reserved_edges(PHLMONITOR monitor);
Hyprutils::Math::Vector2D logical_monitor_size(PHLMONITOR monitor);
Box                       logical_monitor_box(PHLMONITOR monitor);
Box                       logical_workarea_box(PHLMONITOR monitor, double outerGap);
Mode                      default_mode_for_monitor(PHLMONITOR monitor);

} // namespace ScrollerCore
