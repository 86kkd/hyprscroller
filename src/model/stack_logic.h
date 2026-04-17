#pragma once

#include <optional>

#include "../list.h"
#include "../core/types.h"

namespace ScrollerModel::StackLogic {

// When the active node is erased, prefer the next sibling and fall back to the previous one.
template <typename T>
ListNode<T> *next_active_after_removal(ListNode<T> *active, ListNode<T> *last) {
    if (!active)
        return nullptr;

    return active != last ? active->next() : active->prev();
}

// Resolve the target local origin for an align command when it applies on this axis.
inline std::optional<double> aligned_local_position(Direction direction,
                                                    Direction backward,
                                                    Direction forward,
                                                    double localOrigin,
                                                    double localSpan,
                                                    double itemSpan) {
    if (direction == backward)
        return localOrigin;
    if (direction == forward)
        return localOrigin + localSpan - itemSpan;
    if (direction == Direction::Center)
        return localOrigin + 0.5 * (localSpan - itemSpan);

    return std::nullopt;
}

} // namespace ScrollerModel::StackLogic
