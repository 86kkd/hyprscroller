/**
 * @file orientation_math.cpp
 * @brief Pure orientation and coordinate-transform helpers.
 */
#include "orientation_math.h"

#include <cmath>

namespace Overview {
namespace {

ScrollerCore::Box round_box(const ScrollerCore::Box& box) {
    return {
        std::round(box.x),
        std::round(box.y),
        std::round(box.w),
        std::round(box.h),
    };
}

ScrollerCore::Box transform_box_landscape(const ScrollerCore::Box& box,
                                          wl_output_transform transform,
                                          double renderWidth,
                                          double renderHeight) {
    switch (transform) {
        case WL_OUTPUT_TRANSFORM_NORMAL:
            return box;
        case WL_OUTPUT_TRANSFORM_180:
            return {renderWidth - (box.x + box.w), renderHeight - (box.y + box.h), box.w, box.h};
        case WL_OUTPUT_TRANSFORM_FLIPPED:
            return {renderWidth - (box.x + box.w), box.y, box.w, box.h};
        case WL_OUTPUT_TRANSFORM_FLIPPED_180:
            return {box.x, renderHeight - (box.y + box.h), box.w, box.h};
        default:
            return box;
    }
}

ScrollerCore::Box transform_box_portrait(const ScrollerCore::Box& box,
                                         wl_output_transform transform,
                                         double renderWidth,
                                         double renderHeight) {
    switch (transform) {
        case WL_OUTPUT_TRANSFORM_90:
            return {box.y, renderHeight - (box.x + box.w), box.h, box.w};
        case WL_OUTPUT_TRANSFORM_270:
            return {renderWidth - (box.y + box.h), box.x, box.h, box.w};
        case WL_OUTPUT_TRANSFORM_FLIPPED_90:
            return {box.y, box.x, box.h, box.w};
        case WL_OUTPUT_TRANSFORM_FLIPPED_270:
            return {renderWidth - (box.y + box.h), renderHeight - (box.x + box.w), box.h, box.w};
        default:
            return box;
    }
}

} // namespace

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

MonitorOrientation orientation_for_transform(wl_output_transform transform) {
    return transform_swaps_axes(transform) ? MonitorOrientation::Portrait : MonitorOrientation::Landscape;
}

std::string_view orientation_name(MonitorOrientation orientation) {
    switch (orientation) {
        case MonitorOrientation::Portrait:
            return "portrait";
        case MonitorOrientation::Landscape:
        default:
            return "landscape";
    }
}

ScrollerCore::Box transform_box_to_render_space(const ScrollerCore::Box& box,
                                                wl_output_transform transform,
                                                double renderWidth,
                                                double renderHeight) {
    const auto transformed = orientation_for_transform(transform) == MonitorOrientation::Portrait
                                 ? transform_box_portrait(box, transform, renderWidth, renderHeight)
                                 : transform_box_landscape(box, transform, renderWidth, renderHeight);
    return round_box(transformed);
}

} // namespace Overview
