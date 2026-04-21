/**
 * @file core.cpp
 * @brief Lane construction, stack ownership, and payload-transfer mechanics.
 *
 * `Lane` is the bridge between canvas-level commands and stack-level layout
 * work. A lane owns ordered `Stack` objects, tracks which stack is active, and
 * implements the "take one active window out here, reinsert it there" flows used
 * by move-window and cross-monitor handoff code.
 *
 * Reading guide:
 * - constructors and cache helpers explain lane ownership rules
 * - payload helpers explain how an active window leaves and re-enters a lane
 * - geometry setters describe how a lane reacts when the canvas moves/resizes
 */
#include "lane.h"

#include <algorithm>
#include <cassert>

#include <hyprland/src/Compositor.hpp>

#include "../../core/layout_profile.h"
#include "../../core/monitor_geometry_runtime.h"
#include "../../core/window_key.h"

using ScrollerCore::Box;
using ScrollerModel::Reorder;
using ScrollerModel::Stack;
using ScrollerModel::StackWidth;

Lane::Lane(PHLWINDOW window)
    : ephemeral(false), gap(0), reorder(Reorder::Auto), mode(Mode::Row), active(nullptr) {
    const auto monitor = g_pCompositor->getMonitorFromID(window->monitorID());
    if (!monitor)
        return;

    mode = ScrollerCore::default_mode_for_monitor(monitor);
    update_sizes(monitor);
}

Lane::Lane(PHLMONITOR monitor, Mode laneMode)
    : ephemeral(false), gap(0), reorder(Reorder::Auto), mode(laneMode), active(nullptr) {
    if (monitor)
        update_sizes(monitor);
}

Lane::Lane(Stack *stack)
    : ephemeral(false), gap(0), reorder(Reorder::Auto), mode(Mode::Row), active(nullptr) {
    // This constructor is primarily used when a whole stack is transferred into
    // a fresh lane. Derive mode/sizes from the stack's active window so the new
    // lane starts in a monitor-consistent coordinate space.
    const auto window = stack ? stack->get_active_window() : nullptr;
    const auto monitor = window ? g_pCompositor->getMonitorFromID(window->monitorID()) : nullptr;
    if (monitor) {
        mode = ScrollerCore::default_mode_for_monitor(monitor);
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

    // `stackByWindow` is a cache over the authoritative stack list. Validate a
    // cached owner before trusting it, then fall back to a whole-lane scan.
    const auto key = ScrollerCore::window_key(window);
    if (auto *cachedStack = stackByWindow.find_valid(key, [&](Stack *owner) {
            return owner && getStackNode(owner) && owner->has_window(window);
        }))
        return cachedStack;

    for (auto col = stacks.first(); col != nullptr; col = col->next()) {
        if (col->data()->has_window(window)) {
            stackByWindow.remember(key, col->data());
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

    stackByWindow.remember(ScrollerCore::window_key(window), stack);
}

void Lane::forgetWindowStack(PHLWINDOW window) {
    if (!window)
        return;

    stackByWindow.forget(ScrollerCore::window_key(window));
}

void Lane::rememberStackWindows(Stack *stack) {
    if (!stack)
        return;

    stackByWindow.remember_owner(stack, [&](auto &&remember) {
        stack->for_each_window([&](PHLWINDOW window) {
            remember(ScrollerCore::window_key(window));
        });
    });
}

void Lane::forgetStackWindows(Stack *stack) {
    if (!stack)
        return;

    stackByWindow.forget_owner(stack);
}

void Lane::debugVerifyStackCache() const {
#ifndef NDEBUG
    assert(stackByWindow.matches_expected([&](auto &&addExpected) {
        for (auto col = stacks.first(); col != nullptr; col = col->next()) {
            auto *stack = col->data();
            stack->for_each_window([&](PHLWINDOW window) {
                addExpected(ScrollerCore::window_key(window), stack);
            });
        }
    }));
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

ActiveWindowRestorePlan Lane::capture_active_window_restore_plan(Direction direction) const {
    ActiveWindowRestorePlan plan;
    plan.direction = direction;

    if (!active)
        return plan;

    auto *stack = active->data();
    if (!stack || stack->size() <= 1)
        return plan;

    // If the active stack still contains siblings after extraction, a failed
    // handoff should put the window back next to those siblings instead of
    // materializing a brand-new stack.
    plan.restoreIntoCurrentStack = true;
    plan.insertBeforeCurrent = stack->active_at_edge(ScrollerCore::stack_item_backward_direction(mode));
    return plan;
}

Stack *Lane::extract_active_stack() {
    if (!active)
        return nullptr;

    // Whole-stack extraction is simpler than window-payload extraction because
    // the stack remains structurally intact; we only need to detach it from the
    // lane list and repair the active pointer/cache.
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

    // This is the central "move one active window out of the lane" primitive.
    // The returned payload must carry both the model window and enough sizing
    // intent for the destination lane to rebuild an equivalent stack.
    auto *stack = active->data();
    ActiveWindowPayload payload;
    // Free-width stacks need to carry their current rendered primary span so a
    // later reinsertion does not collapse back to the lane default preset.
    payload.width = stack->get_width();
    payload.primarySpan = stack_primary_span_for_transfer(stack);
    const auto payloadWindow = stack->get_active_window();

    payload.window = stack->expel_active(gap);
    if (!payload.window)
        return {};

    forgetWindowStack(payloadWindow);
    reorder = Reorder::Auto;

    if (stack->size() == 0) {
        // If the extracted window was the last member of its stack, the lane no
        // longer owns that stack at all. Remove the empty stack immediately so
        // later relayout code only sees live stacks.
        auto *emptyNode = active;
        active = emptyNode == stacks.last() ? emptyNode->prev() : emptyNode->next();
        stacks.erase(emptyNode);
        forgetStackWindows(stack);
        delete stack;
        debugVerifyStackCache();
        return payload;
    }

    stack->fit_size(FitSize::All, calculate_gap_x(active), gap);
    debugVerifyStackCache();
    return payload;
}

double Lane::stack_primary_span_for_transfer(const Stack *stack) const {
    if (!stack)
        return 0.0;

    // Preset-width stacks can recompute their span from the destination lane's
    // bounds. Only free-width stacks need to carry an explicit axis span.
    if (stack->get_width() != StackWidth::Free)
        return mode == Mode::Column ? max.h : max.w;

    return mode == Mode::Column ? stack->get_geom_h() : stack->get_geom_w();
}

Stack *Lane::create_stack_from_payload(ActiveWindowPayload payload) {
    if (!payload)
        return nullptr;

    // Rebuild a single-window stack in this lane's orientation. The payload is
    // already detached from the source lane, so from here on the destination
    // lane becomes the sole owner of the transferred model window.
    auto window = payload.release_window();
    if (!window)
        return nullptr;

    window->set_geom_h(mode == Mode::Column ? max.w : max.h);
    window->set_geom_y(mode == Mode::Column ? max.x : max.y);

    // `primarySpan` refers to the lane axis: width in row mode, height in
    // column mode. Keep the name axis-neutral so transfer code reads the same
    // in both orientations.
    const auto targetPrimarySpan =
        payload.width == StackWidth::Free && payload.primarySpan > 0.0
            ? std::min(payload.primarySpan, mode == Mode::Column ? max.h : max.w)
            : (mode == Mode::Column ? max.h : max.w);
    return new Stack(std::move(window),
                     payload.width,
                     mode == Mode::Column ? max.w : targetPrimarySpan,
                     mode == Mode::Column ? targetPrimarySpan : max.h,
                     mode);
}

void Lane::position_stack_relative_to_reference(Stack *stack, const Stack *reference, Direction direction) {
    if (!stack || !reference)
        return;

    const auto backward = ScrollerCore::local_item_backward_direction(mode);
    if (mode == Mode::Column) {
        const auto y = direction == backward
            ? reference->get_geom_y() - stack->get_geom_h()
            : reference->get_geom_y() + reference->get_geom_h();
        stack->set_geom_pos(max.x, y);
        return;
    }

    const auto x = direction == backward
        ? reference->get_geom_x() - stack->get_geom_w()
        : reference->get_geom_x() + reference->get_geom_w();
    stack->set_geom_pos(x, max.y);
}

void Lane::insert_window_payload(ActiveWindowPayload payload, Direction direction) {
    if (!payload)
        return;

    // Insertion always resets reorder policy because the lane structure has
    // materially changed and later relayout should be free to normalize it.
    reorder = Reorder::Auto;
    const bool singleWindowLane = stacks.size() == 1 && stacks.first()->data()->size() == 1;
    if (singleWindowLane)
        stacks.first()->data()->update_width(StackWidth::OneHalf, max.w, max.h);

    auto *stack = create_stack_from_payload(std::move(payload));
    if (!stack)
        return;
    const auto compositorWindow = stack->get_active_window();
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

    // Insert relative to the current active stack, then repair widths when row
    // mode would otherwise overflow the visible lane width.
    auto *current = active;
    auto *currentStack = current->data();
    if (currentStack->expanded())
        (void)currentStack->toggle_fullscreen(max);

    auto inserted = stacks.emplace_after(current, stack);
    if (direction == Direction::Left || direction == Direction::Up || direction == Direction::Begin)
        stacks.move_before(current, inserted);

    if (mode == Mode::Row) {
        const auto currentWidth = currentStack->get_geom_w();
        const auto insertedWidth = stack->get_geom_w();
        const auto totalWidth = currentWidth + insertedWidth;
        if (totalWidth <= max.w || currentWidth <= 0.0 || insertedWidth <= 0.0) {
            active = inserted;
            rememberWindowStack(compositorWindow, stack);
            recalculate_lane_geometry();
            debugVerifyStackCache();
            return;
        }

        // Two free-width stacks can easily overflow a row lane. Scale both
        // widths down proportionally so insertion preserves relative intent but
        // still fits the lane box.
        const auto scale = max.w / totalWidth;
        currentStack->set_width_free();
        currentStack->set_geom_w(currentWidth * scale);
        stack->set_width_free();
        stack->set_geom_w(insertedWidth * scale);
    }

    active = inserted;
    rememberWindowStack(compositorWindow, stack);
    recalculate_lane_geometry();
    debugVerifyStackCache();
}

void Lane::restore_active_window_payload(ActiveWindowPayload payload, const ActiveWindowRestorePlan &plan) {
    if (!payload)
        return;

    // Failed handoff fallback: either rebuild a standalone stack or put the
    // window back into the original active stack, depending on what was
    // captured before extraction.
    if (!plan.restoreIntoCurrentStack || !active || !active->data()) {
        insert_window_payload(std::move(payload), plan.direction);
        return;
    }

    auto *stack = active->data();
    auto window = payload.release_window();
    const auto compositorWindow = window ? window->ptr().lock() : nullptr;

    stack->restore_window(std::move(window), plan.insertBeforeCurrent);
    if (compositorWindow)
        rememberWindowStack(compositorWindow, stack);

    reorder = Reorder::Auto;
    recalculate_lane_geometry();
    debugVerifyStackCache();
}

void Lane::set_canvas_geometry(const Box &full_box, const Box &max_box, int gap_size) {
    // Lanes store stack-local positions relative to the canvas workarea. If the
    // workarea origin shifts, every owned stack must shift by the same delta so
    // their local coordinates remain visually stable after relayout.
    const auto previousLocalOrigin = mode == Mode::Column ? max.x : max.y;
    const auto nextLocalOrigin = mode == Mode::Column ? max_box.x : max_box.y;
    const auto localDelta = nextLocalOrigin - previousLocalOrigin;
    if (localDelta != 0.0) {
        for (auto stack = stacks.first(); stack != nullptr; stack = stack->next())
            stack->data()->shift_local_geometry(localDelta);
    }

    full = full_box;
    max = max_box;
    gap = gap_size;
}
