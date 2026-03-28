/**
 * @file geometry.cpp
 * @brief Lane geometry, overview projection, and viewport relayout helpers.
 *
 * This file contains the geometry-heavy part of lane behavior: stack visibility
 * checks, anchor selection, overview projection, fullscreen/maximize layout,
 * and the final relayout pass that keeps the active stack visible.
 */
#include "lane.h"

#include <cmath>
#include <sstream>
#include <vector>

#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/config/ConfigManager.hpp>
#include <hyprland/src/config/ConfigValue.hpp>
#include <hyprland/src/plugins/PluginAPI.hpp>
#include <spdlog/spdlog.h>
#ifdef COLORS_IPC
#include <hyprland/src/managers/EventManager.hpp>
#endif

#include "../../core/interval.h"
#include "../../core/layout_math.h"
#include "../../core/layout_profile.h"
#include "../canvas/internal.h"

namespace {
double stack_primary_origin(const Stack *stack, Mode mode) {
    return mode == Mode::Column ? stack->get_geom_y() : stack->get_geom_x();
}

double stack_primary_span(const Stack *stack, Mode mode) {
    return mode == Mode::Column ? stack->get_geom_h() : stack->get_geom_w();
}

double visible_primary_origin(const ScrollerCore::Box &visible_box, Mode mode) {
    return mode == Mode::Column ? visible_box.y : visible_box.x;
}

double visible_primary_span(const ScrollerCore::Box &visible_box, Mode mode) {
    return mode == Mode::Column ? visible_box.h : visible_box.w;
}

double visible_primary_end(const ScrollerCore::Box &visible_box, Mode mode) {
    return visible_primary_origin(visible_box, mode) + visible_primary_span(visible_box, mode);
}

void set_stack_primary_position(Stack *stack, Mode mode, const ScrollerCore::Box &visible_box, double primary_pos) {
    if (!stack)
        return;

    if (mode == Mode::Column)
        stack->set_geom_pos(visible_box.x, primary_pos);
    else
        stack->set_geom_pos(primary_pos, visible_box.y);
}

namespace viewport {
// Return true when a stack would intersect the visible viewport at a projected position.
bool projected_stack_intersects_visible_box(const Stack *stack, const double projected_pos,
                                            const ScrollerCore::Box &visible_box, Mode mode) {
    if (!stack)
        return false;

    const auto projected_end = projected_pos + stack_primary_span(stack, mode);
    return ScrollerCore::Interval::intersects(projected_pos, projected_end,
                                              visible_primary_origin(visible_box, mode),
                                              visible_primary_end(visible_box, mode));
}

} // namespace viewport

namespace logging {
// Return the currently active compositor window pointer for readable logs.
const void* active_window_ptr(Stack *stack) {
    if (!stack)
        return nullptr;

    const auto window = stack->get_active_window();
    return static_cast<const void*>(window ? window.get() : nullptr);
}

// Summarize the stack list for row-relayout debugging logs.
std::string summarize_stacks(List<Stack *>& stacks, Mode mode) {
    std::ostringstream out;
    for (auto col = stacks.first(); col != nullptr; col = col->next()) {
        if (col != stacks.first())
            out << " | ";

        auto *data = col->data();
        out << active_window_ptr(data)
            << (mode == Mode::Column ? "@y=" : "@x=")
            << (data ? stack_primary_origin(data, mode) : 0.0)
            << (mode == Mode::Column ? ",h=" : ",w=")
            << (data ? stack_primary_span(data, mode) : 0.0);
	}
	return out.str();
}
} // namespace logging

namespace overview {
// Compute the scaled bounding box and offset required for overview mode.
ScrollerCore::OverviewProjection compute_projection(List<Stack *>& stacks, const ScrollerCore::Box &visible_box) {
    std::vector<ScrollerCore::OverviewRect> items;
    items.reserve(stacks.size());
    for (auto stack = stacks.first(); stack != nullptr; stack = stack->next()) {
        auto x0 = stack->data()->get_geom_x();
        auto x1 = x0 + stack->data()->get_geom_w();
        Vector2D height = stack->data()->get_height();
        items.push_back(ScrollerCore::OverviewRect{
            .x0 = x0,
            .x1 = x1,
            .y0 = height.x,
            .y1 = height.y,
        });
    }

    const auto projection = ScrollerCore::compute_overview_projection(items, visible_box);
    if (projection.width <= 0.0 || projection.height <= 0.0) {
        spdlog::debug("overview_projection_degenerate: width={} height={} visible_box=({}, {}, {}, {})",
                      projection.width,
                      projection.height,
                      visible_box.x,
                      visible_box.y,
                      visible_box.w,
                      visible_box.h);
    }
    return projection;
}

// Apply overview projection to every stack in the lane.
void apply_projection(List<Stack *>& stacks, const ScrollerCore::OverviewProjection &projection,
                      double gap, const ScrollerCore::Box &visible_box) {
    for (auto stack = stacks.first(); stack != nullptr; stack = stack->next()) {
        Stack *column = stack->data();
        column->push_geom();
        Vector2D height = column->get_height();
        Vector2D start(projection.offset.x + visible_box.x, projection.offset.y + visible_box.y);
        column->set_geom_pos(start.x + (column->get_geom_x() - projection.min.x) * projection.scale,
                             start.y + (height.x - projection.min.y) * projection.scale);
        column->set_geom_w(column->get_geom_w() * projection.scale);
        column->set_geom_h(column->get_geom_h() * projection.scale);
        column->scale(projection.min, start, projection.scale, gap);
    }
}

// Restore normal geometry after overview mode ends.
void restore_projection(List<Stack *>& stacks, ListNode<Stack *> *active, const ScrollerCore::Box &visible_box) {
    for (auto stack = stacks.first(); stack != nullptr; stack = stack->next())
        stack->data()->pop_geom();

    Stack *activeStack = active->data();
    const auto mode = activeStack->get_mode();
    const auto primaryOrigin = stack_primary_origin(activeStack, mode);
    const auto primarySpan = stack_primary_span(activeStack, mode);
    if (primaryOrigin < visible_primary_origin(visible_box, mode)) {
        set_stack_primary_position(activeStack, mode, visible_box, visible_primary_origin(visible_box, mode));
    } else if (primaryOrigin + primarySpan > visible_primary_end(visible_box, mode)) {
        set_stack_primary_position(activeStack, mode, visible_box,
                                   visible_primary_end(visible_box, mode) - primarySpan);
    }
}
} // namespace overview

namespace recalc {
// Initialize active-stack geometry when a stack is placed for the first time.
double initialize_active_stack_geometry(ListNode<Stack *> *active, const ScrollerCore::Box &visible_box,
                                        double active_span, Mode mode) {
    if (active->data()->get_init())
        return stack_primary_origin(active->data(), mode);

    double active_pos;
    if (active->prev()) {
        Stack *prev = active->prev()->data();
        active_pos = stack_primary_origin(prev, mode) + stack_primary_span(prev, mode);
    } else if (active->next()) {
        active_pos = stack_primary_origin(active->data(), mode);
    } else {
        active_pos = visible_primary_origin(visible_box, mode) + 0.5 * (visible_primary_span(visible_box, mode) - active_span);
    }

    active->data()->set_init();
    return active_pos;
}
} // namespace recalc
} // namespace

// Compute the gap pair on the axis orthogonal to the stack's local window flow.
Vector2D Lane::calculate_gap_x(const ListNode<Stack *> *stack) const {
    auto gap0 = stack == stacks.first() ? 0.0 : gap;
    auto gap1 = stack == stacks.last() ? 0.0 : gap;
    return Vector2D(gap0, gap1);
}

// Center the active stack inside the lane according to its width mode.
void Lane::center_active_stack() {
    if (!active)
        return;

    Stack *stack = active->data();
    if (stack->maximized())
        return;

    if (mode == Mode::Column) {
        switch (stack->get_width()) {
        case StackWidth::OneThird:
            stack->set_geom_pos(max.x, max.y + max.h / 3.0);
            break;
        case StackWidth::OneHalf:
            stack->set_geom_pos(max.x, max.y + max.h / 4.0);
            break;
        case StackWidth::TwoThirds:
            stack->set_geom_pos(max.x, max.y + max.h / 6.0);
            break;
        case StackWidth::Free:
            stack->set_geom_pos(max.x, max.y + 0.5 * (max.h - stack->get_geom_h()));
            break;
        default:
            break;
        }
        return;
    }

    switch (stack->get_width()) {
    case StackWidth::OneThird:
        stack->set_geom_pos(max.x + max.w / 3.0, max.y);
        break;
    case StackWidth::OneHalf:
        stack->set_geom_pos(max.x + max.w / 4.0, max.y);
        break;
    case StackWidth::TwoThirds:
        stack->set_geom_pos(max.x + max.w / 6.0, max.y);
        break;
    case StackWidth::Free:
        stack->set_geom_pos(0.5 * (max.w - stack->get_geom_w()), max.y);
        break;
    default:
        break;
    }
}

// Predict the initial size for a new window inserted into this lane.
Vector2D Lane::predict_window_size() const {
    return ScrollerCore::predict_window_size(mode, max);
}

// Refresh lane bounds from the monitor workarea and gap configuration.
void Lane::update_sizes(PHLMONITOR monitor) {
    if (!monitor)
        return;

    const auto bounds = CanvasLayoutInternal::compute_canvas_bounds(monitor);
    full = bounds.full;
    max = bounds.max;
    gap = bounds.gap;
}

// Force the active stack into fullscreen geometry used for special fullscreen paths.
void Lane::set_fullscreen_active_window() {
    if (!active)
        return;

    active->data()->set_fullscreen(full);
    active->data()->recalculate_stack_geometry(calculate_gap_x(active), gap);
}

// Toggle scroller-managed fullscreen on the active stack and relayout.
void Lane::toggle_fullscreen_active_window() {
    if (!active)
        return;

    Stack *stack = active->data();
    (void)stack->toggle_fullscreen(max, mode);
    recalculate_lane_geometry();
}

// Toggle maximized width/height behavior for the active stack.
void Lane::toggle_maximize_active_stack() {
    if (!active)
        return;

    Stack *stack = active->data();
    stack->toggle_maximized(max.w, max.h);
    reorder = Reorder::Auto;
    recalculate_lane_geometry();
}

// Toggle overview mode for the whole lane.
void Lane::toggle_overview() {
    if (stacks.empty() || !active)
        return;

    overview = !overview;
    if (overview) {
        const auto projection = overview::compute_projection(stacks, max);
        overview::apply_projection(stacks, projection, gap, max);
        adjust_stacks(stacks.first());
        return;
    }

    overview::restore_projection(stacks, active, max);
    adjust_stacks(active);
}

// Main geometry pass for a lane: keep the active stack visible and reposition
// neighbors around it.
void Lane::recalculate_lane_geometry() {
    if (active == nullptr)
        return;

    if (const auto activeWindow = active->data()->get_active_window(); activeWindow && activeWindow->isFullscreen()) {
        active->data()->recalculate_stack_geometry(calculate_gap_x(active), gap);
        return;
    }
#ifdef COLORS_IPC
    static auto *const FREECOLUMN = (CGradientValueData *) HyprlandAPI::getConfigValue(PHANDLE, "plugin:scroller:col.freecolumn_border")->data.get();
    static auto *const ACTIVECOL = (CGradientValueData *)g_pConfigManager->getConfigValuePtr("general:col.active_border")->data.get();
    if (const auto activeWindow = active->data()->get_active_window()) {
        if (active->data()->get_width() == StackWidth::Free) {
            activeWindow->m_cRealBorderColor = *FREECOLUMN;
        } else {
            activeWindow->m_cRealBorderColor = *ACTIVECOL;
        }
    }
    g_pEventManager->postEvent(SHyprIPCEvent{"scroller", active->data()->get_width_name() + "," + active->data()->get_height_name()});
#endif
    if (stacks.size() == 1 && active->data()->size() == 1) {
        auto *stack = active->data();
        stack->update_width(stack->get_width(), max.w, max.h);
        stack->set_geom_pos(max.x, max.y);
        stack->set_geom_w(max.w);
        stack->set_geom_h(max.h);
        stack->fit_size(FitSize::All, calculate_gap_x(active), gap);
        stack->recalculate_stack_geometry(calculate_gap_x(active), gap);
        spdlog::debug("lane_recalc_single: active_window={} stacks={}",
                      logging::active_window_ptr(stack),
                      logging::summarize_stacks(stacks, mode));
        return;
    }

    auto activeSpan = stack_primary_span(active->data(), mode);
    auto activePos = recalc::initialize_active_stack_geometry(active, max, activeSpan, mode);
    spdlog::debug("lane_recalc_input: active_window={} active_pos={} active_span={} max=({}, {}, {}, {}) stacks_before={}",
                  logging::active_window_ptr(active->data()),
                  activePos,
                  activeSpan,
                  max.x,
                  max.y,
                  max.w,
                  max.h,
                  logging::summarize_stacks(stacks, mode));
    if (activePos < visible_primary_origin(max, mode)) {
        activePos = visible_primary_origin(max, mode);
        set_stack_primary_position(active->data(), mode, max, activePos);
        adjust_stacks(active);
        spdlog::debug("lane_recalc_clamp_before: active_window={} active_pos={} stacks_after={}",
                      logging::active_window_ptr(active->data()),
                      stack_primary_origin(active->data(), mode),
                      logging::summarize_stacks(stacks, mode));
        return;
    }
    if (std::round(activePos + activeSpan) > visible_primary_end(max, mode)) {
        activePos = visible_primary_end(max, mode) - activeSpan;
        set_stack_primary_position(active->data(), mode, max, activePos);
        adjust_stacks(active);
        spdlog::debug("lane_recalc_clamp_after: active_window={} active_pos={} stacks_after={}",
                      logging::active_window_ptr(active->data()),
                      stack_primary_origin(active->data(), mode),
                      logging::summarize_stacks(stacks, mode));
        return;
    }
    if (reorder != Reorder::Auto) {
        set_stack_primary_position(active->data(), mode, max, activePos);
        adjust_stacks(active);
        spdlog::debug("lane_recalc_lazy: active_window={} active_pos={} stacks_after={}",
                      logging::active_window_ptr(active->data()),
                      stack_primary_origin(active->data(), mode),
                      logging::summarize_stacks(stacks, mode));
        return;
    }

    const Box active_window(max.x, max.y, max.w, max.h);
    const auto *prev = active->prev() ? active->prev()->data() : nullptr;
    const auto *next = active->next() ? active->next()->data() : nullptr;
    const auto prevPos = prev ? activePos - stack_primary_span(prev, mode) : 0.0;
    const auto nextPos = activePos + activeSpan;
    const bool prev_inside = viewport::projected_stack_intersects_visible_box(prev, prevPos, active_window, mode);
    const bool next_inside = viewport::projected_stack_intersects_visible_box(next, nextPos, active_window, mode);
    const bool keep_current = prev_inside || next_inside;
    const auto prevSpan = prev ? stack_primary_span(prev, mode) : 0.0;
    const auto nextSpan = next ? stack_primary_span(next, mode) : 0.0;
    const double newPos = keep_current
        ? activePos
        : (mode == Mode::Column
               ? ScrollerCore::choose_anchor_y(next != nullptr, prev != nullptr, activeSpan, nextSpan, prevSpan, max)
               : ScrollerCore::choose_anchor_x(next != nullptr, prev != nullptr, activeSpan, nextSpan, prevSpan, activePos, max));
    set_stack_primary_position(active->data(), mode, max, newPos);
    adjust_stacks(active);
    spdlog::debug("lane_recalc_auto: active_window={} keep_current={} prev_inside={} next_inside={} new_pos={} stacks_after={}",
                  logging::active_window_ptr(active->data()),
                  keep_current,
                  prev_inside,
                  next_inside,
                  newPos,
                  logging::summarize_stacks(stacks, mode));
}

void Lane::adjust_stacks(ListNode<Stack *> *stack) {
    for (auto col = stack->prev(), prev = stack; col != nullptr; prev = col, col = col->prev()) {
        set_stack_primary_position(col->data(), mode, max,
                                   stack_primary_origin(prev->data(), mode) - stack_primary_span(col->data(), mode));
        col->data()->set_init();
    }
    for (auto col = stack->next(), prev = stack; col != nullptr; prev = col, col = col->next()) {
        set_stack_primary_position(col->data(), mode, max,
                                   stack_primary_origin(prev->data(), mode) + stack_primary_span(prev->data(), mode));
        col->data()->set_init();
    }

    stack->data()->set_init();

    for (auto col = stacks.first(); col != nullptr; col = col->next()) {
        auto gap0 = col == stacks.first() ? 0.0 : gap;
        auto gap1 = col == stacks.last() ? 0.0 : gap;
        col->data()->recalculate_stack_geometry(Vector2D(gap0, gap1), gap);
    }
}
