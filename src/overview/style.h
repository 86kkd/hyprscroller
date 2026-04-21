/**
 * @file style.h
 * @brief Shared overview layout and rendering constants.
 */
#pragma once

#include <hyprland/src/helpers/Color.hpp>

namespace Overview::Style {

inline constexpr double kWorkspaceContentInset = 14.0;
inline constexpr double kWorkspaceHeaderHeight = 26.0;
inline constexpr double kPreviewInset = 2.0;
inline constexpr double kTargetScaleBase = 0.97;
inline constexpr double kTargetScaleRange = 0.03;
inline constexpr double kTitleBackdropInset = 10.0;
inline constexpr double kTitleBackdropMinWidth = 80.0;
inline constexpr double kTitleBackdropMaxWidthRatio = 0.72;
inline constexpr double kTitleBackdropMinHeight = 20.0;
inline constexpr double kTitleBackdropMaxHeight = 28.0;
inline constexpr double kTitleBackdropHeightRatio = 0.12;
inline constexpr double kTitleTextInsetX = 8.0;
inline constexpr double kTitleTextInsetY = 3.0;
inline constexpr double kSelectionOutlineScale = 1.02;
inline constexpr double kOutlineBorderWidth = 2.0;
inline constexpr int    kDefaultRound = 12;
inline constexpr int    kEmptyTargetRound = 20;
inline constexpr int    kTitleBackdropRound = 9;

inline CHyprColor matteBackground() {
    return CHyprColor(0.02F, 0.03F, 0.05F, 1.0F);
}

inline CHyprColor previewBorder(bool selected, float alpha) {
    return selected ? CHyprColor(0.64F, 0.86F, 0.98F, alpha) : CHyprColor(0.28F, 0.31F, 0.38F, alpha);
}

inline CHyprColor previewFallbackFill(bool selected, float alpha) {
    return selected ? CHyprColor(0.16F, 0.22F, 0.29F, alpha) : CHyprColor(0.08F, 0.10F, 0.14F, alpha);
}

inline CHyprColor previewShadow(float alpha) {
    return CHyprColor(0.00F, 0.00F, 0.00F, alpha);
}

inline CHyprColor titleBackdrop(float alpha) {
    return CHyprColor(0.03F, 0.04F, 0.06F, alpha);
}

inline CHyprColor titleText(float alpha) {
    return CHyprColor(0.95F, 0.97F, 1.0F, alpha);
}

inline CHyprColor selectionOutline(float alpha) {
    return CHyprColor(0.91F, 0.76F, 0.27F, alpha);
}

} // namespace Overview::Style
