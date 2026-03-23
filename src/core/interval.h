/**
 * @file interval.h
 * @brief Small reusable 1D interval predicates shared by layout geometry code.
 *
 * The scrolling layout frequently answers the same question on both axes:
 * whether a segment is visible inside a viewport, and whether it is fully
 * contained by that viewport. Keeping these predicates here avoids duplicating
 * slightly different X/Y implementations in lane and stack geometry code.
 */
#pragma once

namespace ScrollerCore::Interval {
/**
 * @brief Return true when a segment intersects a viewport interval.
 */
inline bool intersects(double start, double end, double viewportStart, double viewportEnd) {
    return (start < viewportEnd && start >= viewportStart) ||
           (end > viewportStart && end <= viewportEnd) ||
           (start < viewportStart && end >= viewportEnd);
}

/**
 * @brief Return true when a segment is fully contained inside a viewport interval.
 */
inline bool fully_visible(double start, double end, double viewportStart, double viewportEnd) {
    return start >= viewportStart && end <= viewportEnd;
}
} // namespace ScrollerCore::Interval
