/**
 * @file overview/scene/layout.h
 * @brief Pure projection helpers for overview scene construction.
 */
#pragma once

#include <span>
#include <vector>

#include "core/types.h"

namespace Overview {

ScrollerCore::Box              localizeGlobalBox(const ScrollerCore::Box& box, double originX, double originY);
ScrollerCore::Box              buildWorkspaceContentBox(const ScrollerCore::Box& workspaceBox);
std::vector<ScrollerCore::Box> projectBoxesToContent(std::span<const ScrollerCore::Box> sourceBoxes,
                                                     const ScrollerCore::Box& contentBox);
std::vector<ScrollerCore::Box> projectGlobalBoxesToContent(std::span<const ScrollerCore::Box> sourceBoxes,
                                                           const ScrollerCore::Box& contentBox,
                                                           double originX,
                                                           double originY);
ScrollerCore::Box              buildEmptyWorkspacePreviewBox(const ScrollerCore::Box& contentBox);

} // namespace Overview
