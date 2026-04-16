/**
 * @file monitor_geometry.cpp
 * @brief Transform-aware monitor and workarea helpers shared by layout code.
 */
#include "monitor_geometry.h"

#include <algorithm>

namespace ScrollerCore {
namespace {

bool transform_swaps_axes(wl_output_transform transform) {
    switch (transform) {
        case WL_OUTPUT_TRANSFORM_90:
        case WL_OUTPUT_TRANSFORM_270:
        case WL_OUTPUT_TRANSFORM_FLIPPED_90:
        case WL_OUTPUT_TRANSFORM_FLIPPED_270:
            return true;
        default:
            return false;
    }
}

} // namespace

Hyprutils::Math::Vector2D logical_monitor_size(const Hyprutils::Math::Vector2D& rawSize,
                                               const Hyprutils::Math::Vector2D& transformedSize,
                                               wl_output_transform transform) {
    if (transformedSize.x > 0.0 && transformedSize.y > 0.0)
        return transformedSize;

    if (transform_swaps_axes(transform))
        return {rawSize.y, rawSize.x};

    return rawSize;
}

Box logical_monitor_box(const Hyprutils::Math::Vector2D& position,
                        const Hyprutils::Math::Vector2D& rawSize,
                        const Hyprutils::Math::Vector2D& transformedSize,
                        wl_output_transform transform) {
    return {position, logical_monitor_size(rawSize, transformedSize, transform)};
}

ReservedEdges logical_reserved_edges(const Hyprutils::Math::Vector2D& rawSize,
                                     const Hyprutils::Math::Vector2D& transformedSize,
                                     wl_output_transform transform,
                                     const ReservedEdges& rawReserved) {
    (void)rawSize;
    (void)transformedSize;
    (void)transform;
    return rawReserved;
}

Box logical_workarea_box(const Hyprutils::Math::Vector2D& position,
                         const Hyprutils::Math::Vector2D& rawSize,
                         const Hyprutils::Math::Vector2D& transformedSize,
                         wl_output_transform transform,
                         const ReservedEdges& rawReserved,
                         double outerGap) {
    const auto full = logical_monitor_box(position, rawSize, transformedSize, transform);
    return {
        full.x + rawReserved.left + outerGap,
        full.y + rawReserved.top + outerGap,
        std::max(0.0, full.w - rawReserved.left - rawReserved.right - 2.0 * outerGap),
        std::max(0.0, full.h - rawReserved.top - rawReserved.bottom - 2.0 * outerGap),
    };
}

} // namespace ScrollerCore
