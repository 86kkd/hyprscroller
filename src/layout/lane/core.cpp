#include "lane.h"

#include <algorithm>

#include <hyprland/src/Compositor.hpp>

Lane::Lane(PHLWINDOW window)
    : mode(Mode::Row), reorder(Reorder::Auto), overview(false), ephemeral(false), active(nullptr) {
    const auto monitor = g_pCompositor->getMonitorFromID(window->monitorID());
    if (!monitor)
        return;

    mode = monitor->m_size.x >= monitor->m_size.y ? Mode::Row : Mode::Column;
    update_sizes(monitor);
}

Lane::Lane(PHLMONITOR monitor, Mode laneMode)
    : mode(laneMode), reorder(Reorder::Auto), overview(false), ephemeral(false), active(nullptr) {
    if (monitor)
        update_sizes(monitor);
}

Lane::Lane(Stack *stack)
    : mode(Mode::Row), reorder(Reorder::Auto), overview(false), ephemeral(false), active(nullptr) {
    const auto window = stack ? stack->get_active_window() : nullptr;
    const auto monitor = window ? g_pCompositor->getMonitorFromID(window->monitorID()) : nullptr;
    if (monitor) {
        mode = monitor->m_size.x >= monitor->m_size.y ? Mode::Row : Mode::Column;
        update_sizes(monitor);
    }

    if (!stack)
        return;

    stacks.push_back(stack);
    active = stacks.first();
}

Lane::~Lane() {
    for (auto col = stacks.first(); col != nullptr; col = col->next()) {
        delete col->data();
    }
    stacks.clear();
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
    for (auto col = stacks.first(); col != nullptr; col = col->next()) {
        if (col->data()->has_window(window))
            return true;
    }
    return false;
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
    active = node != stacks.last() ? node->next() : node->prev();
    stacks.erase(node);
    return stack;
}

ActiveWindowPayload Lane::extract_active_window_payload() {
    if (!active)
        return {};

    auto *stack = active->data();
    ActiveWindowPayload payload = {
        .window = nullptr,
        .width = stack->get_width(),
        .maxw = stack->get_width() == StackWidth::Free ? stack->get_geom_w() : max.w,
    };

    payload.window = stack->expel_active(gap);
    if (!payload.window)
        return {};

    if (stack->size() != 0) {
        reorder = Reorder::Auto;
        stack->recalculate_stack_geometry(calculate_gap_x(active), gap);
        return payload;
    }

    auto emptyNode = active;
    active = emptyNode == stacks.last() ? emptyNode->prev() : emptyNode->next();
    delete stack;
    stacks.erase(emptyNode);
    reorder = Reorder::Auto;
    return payload;
}

void Lane::insert_window_payload(const ActiveWindowPayload& payload, Direction direction) {
    if (!payload)
        return;

    reorder = Reorder::Auto;
    if (mode == Mode::Column && active) {
        const auto windowCountBefore = active->data()->size();
        active->data()->admit_window(payload.window);
        if (windowCountBefore == 1) {
            active->data()->fit_size(FitSize::All, calculate_gap_x(active), gap);
        } else {
            active->data()->recalculate_stack_geometry(calculate_gap_x(active), gap);
        }
        return;
    }

    const bool singleWindowLane = stacks.size() == 1 && stacks.first()->data()->size() == 1;
    if (singleWindowLane)
        stacks.first()->data()->update_width(StackWidth::OneHalf, max.w, max.h);

    payload.window->set_geom_h(max.h);
    payload.window->set_geom_y(max.y);
    const auto targetMaxWidth =
        payload.width == StackWidth::Free && payload.maxw > 0.0
            ? std::min(payload.maxw, max.w)
            : max.w;
    auto *stack = new Stack(payload.window, payload.width, targetMaxWidth, max.h);
    if (singleWindowLane)
        stack->update_width(StackWidth::OneHalf, max.w, max.h);
    stack->set_geom_pos(max.x, max.y);
    if (!active) {
        stacks.push_back(stack);
        active = stacks.last();
        recalculate_lane_geometry();
        return;
    }

    auto current = active;
    if (mode == Mode::Row && current->data()->expanded())
        (void)current->data()->toggle_fullscreen(max, mode);

    auto inserted = stacks.emplace_after(current, stack);
    if (direction == Direction::Left || direction == Direction::Up || direction == Direction::Begin)
        stacks.move_before(current, inserted);

    if (mode == Mode::Row) {
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
    recalculate_lane_geometry();
}

void Lane::set_canvas_geometry(const Box &full_box, const Box &max_box, int gap_size) {
    full = full_box;
    max = max_box;
    gap = gap_size;
}
