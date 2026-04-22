/**
 * @file scene_layout.cpp
 * @brief Pure projection helpers for overview scene construction.
 */
#include "scene_layout.h"

#include <algorithm>
#include <vector>

#include "../core/layout_math.h"
#include "geometry_utils.h"

namespace Overview {
namespace {

using ScrollerCore::OverviewProjection;

// Keep the pure scene-layout helpers free of rendering/color dependencies so
// logic tests can link them without pulling in Hyprland's OpenGL stack.
constexpr double kWorkspaceContentInset = 14.0;
constexpr double kWorkspaceHeaderHeight = 26.0;

OverviewProjection computeProjection(std::span<const ScrollerCore::Box> items, const ScrollerCore::Box& visibleBox) {
    std::vector<ScrollerCore::OverviewRect> rects;
    rects.reserve(items.size());

    for (const auto& item : items) {
        rects.push_back({
            .x0 = item.x,
            .x1 = item.x + item.w,
            .y0 = item.y,
            .y1 = item.y + item.h,
        });
    }

    return ScrollerCore::compute_overview_projection(rects, visibleBox);
}

ScrollerCore::Box applyProjection(const ScrollerCore::Box& source,
                                  const ScrollerCore::Box& visibleBox,
                                  const OverviewProjection& projection) {
    if (projection.width <= 0.0 || projection.height <= 0.0)
        return visibleBox;

    return {
        visibleBox.x + projection.offset.x + (source.x - projection.min.x) * projection.scale,
        visibleBox.y + projection.offset.y + (source.y - projection.min.y) * projection.scale,
        std::max(24.0, source.w * projection.scale),
        std::max(24.0, source.h * projection.scale),
    };
}

} // namespace

ScrollerCore::Box buildWorkspaceContentBox(const ScrollerCore::Box& workspaceBox) {
    auto contentBox = insetBox(workspaceBox,
                               kWorkspaceContentInset,
                               kWorkspaceContentInset);
    contentBox.y += kWorkspaceHeaderHeight;
    contentBox.h = std::max(36.0, contentBox.h - kWorkspaceHeaderHeight);
    return contentBox;
}

std::vector<ScrollerCore::Box> projectBoxesToContent(std::span<const ScrollerCore::Box> sourceBoxes,
                                                     const ScrollerCore::Box& contentBox) {
    const auto projection = computeProjection(sourceBoxes, contentBox);

    std::vector<ScrollerCore::Box> projected;
    projected.reserve(sourceBoxes.size());
    for (const auto& sourceBox : sourceBoxes)
        projected.push_back(applyProjection(sourceBox, contentBox, projection));

    return projected;
}

std::vector<ScrollerCore::Box> projectGlobalBoxesToContent(std::span<const ScrollerCore::Box> sourceBoxes,
                                                           const ScrollerCore::Box& contentBox,
                                                           double originX,
                                                           double originY) {
    std::vector<ScrollerCore::Box> localized;
    localized.reserve(sourceBoxes.size());
    for (const auto& sourceBox : sourceBoxes)
        localized.push_back(translateBox(sourceBox, -originX, -originY));

    const auto projectedLocal = projectBoxesToContent(localized, translateBox(contentBox, -originX, -originY));

    std::vector<ScrollerCore::Box> projected;
    projected.reserve(projectedLocal.size());
    for (const auto& box : projectedLocal)
        projected.push_back(translateBox(box, originX, originY));

    return projected;
}

ScrollerCore::Box buildEmptyWorkspacePreviewBox(const ScrollerCore::Box& contentBox) {
    return insetBox(contentBox, std::max(12.0, contentBox.w * 0.12), std::max(12.0, contentBox.h * 0.14));
}

} // namespace Overview
