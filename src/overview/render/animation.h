/**
 * @file animation.h
 * @brief Shared animation timing and easing helpers for overview rendering.
 */
#pragma once

#include <algorithm>
#include <chrono>

namespace Overview {

using steady_tp = std::chrono::steady_clock::time_point;

inline constexpr auto kOpenDuration = std::chrono::milliseconds(180);
inline constexpr auto kSelectionDuration = std::chrono::milliseconds(120);

inline double clamp01(double value) {
    return std::clamp(value, 0.0, 1.0);
}

inline double easeOutCubic(double t) {
    const auto clamped = clamp01(t);
    const auto inverse = 1.0 - clamped;
    return 1.0 - inverse * inverse * inverse;
}

} // namespace Overview
