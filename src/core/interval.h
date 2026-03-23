#pragma once

namespace ScrollerCore::Interval {
inline bool intersects(double start, double end, double viewportStart, double viewportEnd) {
    return (start < viewportEnd && start >= viewportStart) ||
           (end > viewportStart && end <= viewportEnd) ||
           (start < viewportStart && end >= viewportEnd);
}

inline bool fully_visible(double start, double end, double viewportStart, double viewportEnd) {
    return start >= viewportStart && end <= viewportEnd;
}
} // namespace ScrollerCore::Interval
