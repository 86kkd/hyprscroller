#include "lane.h"

#include <algorithm>
#include <cassert>

#include <hyprland/src/Compositor.hpp>

#include "../../core/layout_profile.h"

Lane::Lane(PHLWINDOW window)
    : overview(false), ephemeral(false), gap(0), reorder(Reorder::Auto), mode(Mode::Row), active(nullptr) {
    const auto monitor = g_pCompositor->getMonitorFromID(window->monitorID());
    if (!monitor)
        return;

    mode = ScrollerCore::default_mode_for_extent(monitor->m_size.x, monitor->m_size.y);
    update_sizes(monitor);
}

Lane::Lane(PHLMONITOR monitor, Mode laneMode)
    : overview(false), ephemeral(false), gap(0), reorder(Reorder::Auto), mode(laneMode), active(nullptr) {
    if (monitor)
        update_sizes(monitor);
}

Lane::Lane(Stack *stack)
    : overview(false), ephemeral(false), gap(0), reorder(Reorder::Auto), mode(Mode::Row), active(nullptr) {
    const auto window = stack ? stack->get_active_window() : nullptr;
    const auto monitor = window ? g_pCompositor->getMonitorFromID(window->monitorID()) : nullptr;
    if (monitor) {
        mode = ScrollerCore::default_mode_for_extent(monitor->m_size.x, monitor->m_size.y);
        update_sizes(monitor);
    }

    if (!stack)
        return;

    stacks.push_back(stack);
    active = stacks.first();
    rememberStackWindows(stack);
    debugVerifyStackCache();
}

Lane::~Lane() {
    for (auto col = stacks.first(); col != nullptr; col = col->next()) {
        delete col->data();
    }
    stacks.clear();
}

Stack *Lane::getStackForWindow(PHLWINDOW window) const {
    if (!window)
        return nullptr;

    const auto key = windowKey(window);
    if (const auto it = stackByWindow.find(key); it != stackByWindow.end()) {
        auto *cachedStack = it->second;
        if (cachedStack && getStackNode(cachedStack) && cachedStack->has_window(window))
            return cachedStack;

        stackByWindow.erase(it);
    }

    for (auto col = stacks.first(); col != nullptr; col = col->next()) {
        if (col->data()->has_window(window)) {
            stackByWindow[key] = col->data();
            return col->data();
        }
    }

    return nullptr;
}

ListNode<Stack *> *Lane::getStackNode(Stack *stack) const {
    if (!stack)
        return nullptr;

    for (auto node = stacks.first(); node != nullptr; node = node->next()) {
        if (node->data() == stack)
            return node;
    }

    return nullptr;
}

void Lane::rememberWindowStack(PHLWINDOW window, Stack *stack) {
    if (!window)
        return;

    if (!stack) {
        forgetWindowStack(window);
        return;
    }

    stackByWindow[windowKey(window)] = stack;
}

void Lane::forgetWindowStack(PHLWINDOW window) {
    if (!window)
        return;

    stackByWindow.erase(windowKey(window));
}

void Lane::rememberStackWindows(Stack *stack) {
    if (!stack)
        return;

    stack->for_each_window([&](PHLWINDOW window) {
        rememberWindowStack(window, stack);
    });
}

void Lane::forgetStackWindows(Stack *stack) {
    if (!stack)
        return;

    for (auto it = stackByWindow.begin(); it != stackByWindow.end();) {
        if (it->second == stack)
            it = stackByWindow.erase(it);
        else
            ++it;
    }
}

void Lane::debugVerifyStackCache() const {
#ifndef NDEBUG
    std::unordered_map<uintptr_t, Stack *> expected;
    for (auto col = stacks.first(); col != nullptr; col = col->next()) {
        auto *stack = col->data();
        stack->for_each_window([&](PHLWINDOW window) {
            const auto [it, inserted] = expected.emplace(windowKey(window), stack);
            assert(inserted);
            assert(it->second == stack);
        });
    }

    assert(expected.size() == stackByWindow.size());
    for (const auto &[key, stack] : expected) {
        const auto it = stackByWindow.find(key);
        assert(it != stackByWindow.end());
        assert(it->second == stack);
    }
#endif
}

bool Lane::empty() const {
    return stacks.empty();
}

bool Lane::is_single_window_lane() const {
    return stacks.size() == 1 && active && active->data()->size() == 1;
}

Mode Lane::get_mode() const {
    return mode;
}

bool Lane::is_ephemeral() const {
    return ephemeral;
}

void Lane::set_ephemeral(bool value) {
    ephemeral = value;
}

bool Lane::has_window(PHLWINDOW window) const {
    return getStackForWindow(window) != nullptr;
}

PHLWINDOW Lane::get_active_window() const {
    if (!active)
        return nullptr;

    return active->data()->get_active_window();
}

bool Lane::is_active(PHLWINDOW window) const {
    return get_active_window() == window;
}

Stack *Lane::extract_active_stack() {
    if (!active)
        return nullptr;

    auto node = active;
    auto stack = node->data();
    forgetStackWindows(stack);
    active = node != stacks.last() ? node->next() : node->prev();
    stacks.erase(node);
    debugVerifyStackCache();
    return stack;
}

ActiveWindowPayload Lane::extract_active_window_payload() {
    if (!active)
        return {};

    auto *stack = active->data();
    ActiveWindowPayload payload;
    payload.width = stack->get_width();
    payload.maxw = stack->get_width() == StackWidth::Free ? stack->get_geom_w() : max.w;
    const auto payloadWindow = stack->get_active_window();

    payload.window = stack->expel_active(gap);
    if (!payload.window)
        return {};

    forgetWindowStack(payloadWindow);

    if (stack->size() != 0) {
        reorder = Reorder::Auto;
        stack->recalculate_stack_geometry(calculate_gap_x(active), gap);
        debugVerifyStackCache();
        return payload;
    }

    auto emptyNode = active;
    active = emptyNode == stacks.last() ? emptyNode->prev() : emptyNode->next();
    stacks.erase(emptyNode);
    forgetStackWindows(stack);
    delete stack;
    reorder = Reorder::Auto;
    debugVerifyStackCache();
    return payload;
}

void Lane::insert_window_payload(ActiveWindowPayload payload, Direction direction) {
    if (!payload)
        return;

    reorder = Reorder::Auto;
    if (ScrollerCore::mode_adds_windows_into_active_stack(mode) && active) {
        auto window = payload.release_window();
        const auto compositorWindow = window ? window->ptr().lock() : nullptr;
        const auto windowCountBefore = active->data()->size();
        active->data()->admit_window(std::move(window));
        rememberWindowStack(compositorWindow, active->data());
        if (windowCountBefore == 1) {
            active->data()->fit_size(FitSize::All, calculate_gap_x(active), gap);
        } else {
            active->data()->recalculate_stack_geometry(calculate_gap_x(active), gap);
        }
        debugVerifyStackCache();
        return;
    }

    const bool singleWindowLane = stacks.size() == 1 && stacks.first()->data()->size() == 1;
    if (singleWindowLane)
        stacks.first()->data()->update_width(StackWidth::OneHalf, max.w, max.h);

    auto window = payload.release_window();
    if (!window)
        return;
    const auto compositorWindow = window->ptr().lock();

    window->set_geom_h(max.h);
    window->set_geom_y(max.y);
    const auto targetMaxWidth =
        payload.width == StackWidth::Free && payload.maxw > 0.0
            ? std::min(payload.maxw, max.w)
            : max.w;
    auto *stack = new Stack(std::move(window), payload.width, targetMaxWidth, max.h);
    if (singleWindowLane)
        stack->update_width(StackWidth::OneHalf, max.w, max.h);
    stack->set_geom_pos(max.x, max.y);
    if (!active) {
        stacks.push_back(stack);
        active = stacks.last();
        rememberWindowStack(compositorWindow, stack);
        recalculate_lane_geometry();
        debugVerifyStackCache();
        return;
    }

    auto current = active;
    if (ScrollerCore::mode_uses_stack_fullscreen(mode) && current->data()->expanded())
        (void)current->data()->toggle_fullscreen(max, mode);

    auto inserted = stacks.emplace_after(current, stack);
    if (direction == Direction::Left || direction == Direction::Up || direction == Direction::Begin)
        stacks.move_before(current, inserted);

    if (ScrollerCore::mode_uses_stack_fullscreen(mode)) {
        auto *currentStack = current->data();
        const auto currentWidth = currentStack->get_geom_w();
        const auto insertedWidth = stack->get_geom_w();
        const auto totalWidth = currentWidth + insertedWidth;
        if (totalWidth > max.w && currentWidth > 0.0 && insertedWidth > 0.0) {
            const auto scale = max.w / totalWidth;
            currentStack->set_width_free();
            currentStack->set_geom_w(currentWidth * scale);
            stack->set_width_free();
            stack->set_geom_w(insertedWidth * scale);
        }
    }

    active = inserted;
    rememberWindowStack(compositorWindow, stack);
    recalculate_lane_geometry();
    debugVerifyStackCache();
}

void Lane::set_canvas_geometry(const Box &full_box, const Box &max_box, int gap_size) {
    full = full_box;
    max = max_box;
    gap = gap_size;
}
