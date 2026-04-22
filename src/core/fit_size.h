#pragma once

#include <utility>

#include "core/intrusive_list.h"
#include "types.h"

namespace ScrollerCore {

// Resolve the inclusive node range affected by a fit-size command.
template <typename T, typename IsVisibleFn>
std::pair<ListNode<T> *, ListNode<T> *> select_fit_size_range(FitSize fitsize,
                                                              ListNode<T> *first,
                                                              ListNode<T> *last,
                                                              ListNode<T> *active,
                                                              IsVisibleFn &&isVisible) {
    switch (fitsize) {
    case FitSize::Active:
        return {active, active};
    case FitSize::Visible: {
        auto *from = static_cast<ListNode<T> *>(nullptr);
        auto *to = static_cast<ListNode<T> *>(nullptr);
        for (auto *node = first; node != nullptr; node = node->next()) {
            if (isVisible(node)) {
                from = node;
                break;
            }
        }
        for (auto *node = last; node != nullptr; node = node->prev()) {
            if (isVisible(node)) {
                to = node;
                break;
            }
        }
        return {from, to};
    }
    case FitSize::All:
        return {first, last};
    case FitSize::ToEnd:
        return {active, last};
    case FitSize::ToBeg:
        return {first, active};
    default:
        return {nullptr, nullptr};
    }
}

// Scale every node in an inclusive range so the combined span matches targetSpan.
template <typename T, typename GetSpanFn, typename SetSpanFn>
bool normalize_fit_size_range(ListNode<T> *from,
                              ListNode<T> *to,
                              double targetSpan,
                              GetSpanFn &&getSpan,
                              SetSpanFn &&setSpan) {
    if (!from || !to || targetSpan <= 0.0)
        return false;

    double totalSpan = 0.0;
    for (auto *node = from; node != nullptr; node = node->next()) {
        totalSpan += getSpan(node);
        if (node == to)
            break;
    }

    if (totalSpan <= 0.0)
        return false;

    const auto scale = targetSpan / totalSpan;
    for (auto *node = from; node != nullptr; node = node->next()) {
        setSpan(node, getSpan(node) * scale);
        if (node == to)
            break;
    }

    return true;
}

} // namespace ScrollerCore
