/**
 * @file monitor_geometry_runtime.cpp
 * @brief Hyprland monitor wrappers over the pure monitor geometry helpers.
 */
#include "monitor_geometry_runtime.h"

#include "layout_profile.h"

namespace ScrollerCore {

ReservedEdges raw_reserved_edges(PHLMONITOR monitor) {
    if (!monitor)
        return {};

    const auto reserved = monitor->m_reservedArea;
    return {
        .top = reserved.top(),
        .right = reserved.right(),
        .bottom = reserved.bottom(),
        .left = reserved.left(),
    };
}

Hyprutils::Math::Vector2D logical_monitor_size(PHLMONITOR monitor) {
    if (!monitor)
        return {};

    return logical_monitor_size(monitor->m_size, monitor->m_transformedSize, monitor->m_transform);
}

Box logical_monitor_box(PHLMONITOR monitor) {
    if (!monitor)
        return {};

    return logical_monitor_box(monitor->m_position, monitor->m_size, monitor->m_transformedSize, monitor->m_transform);
}

Box logical_workarea_box(PHLMONITOR monitor, double outerGap) {
    if (!monitor)
        return {};

    return logical_workarea_box(monitor->m_position,
                                monitor->m_size,
                                monitor->m_transformedSize,
                                monitor->m_transform,
                                raw_reserved_edges(monitor),
                                outerGap);
}

Mode default_mode_for_monitor(PHLMONITOR monitor) {
    const auto size = logical_monitor_size(monitor);
    return default_mode_for_extent(size.x, size.y);
}

} // namespace ScrollerCore
