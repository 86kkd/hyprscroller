/**
 * @file scene_layout.h
 * @brief Pure projection helpers for overview scene construction.
 */
#pragma once

#include <span>
#include <vector>

#include "../core/types.h"

namespace Overview {

std::vector<ScrollerCore::Box> projectBoxesToContent(std::span<const ScrollerCore::Box> sourceBoxes,
                                                     const ScrollerCore::Box& contentBox);
ScrollerCore::Box              buildEmptyWorkspacePreviewBox(const ScrollerCore::Box& contentBox);

} // namespace Overview
