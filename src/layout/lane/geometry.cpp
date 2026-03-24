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
#include "../canvas/internal.h"

namespace {
namespace viewport {
// Return true when a stack would intersect the visible viewport at a projected X.
bool projected_stack_intersects_visible_box(const Stack *stack, const double projected_x,
                                            const ScrollerCore::Box &visible_box) {
    if (!stack)
        return false;

    const auto right = projected_x + stack->get_geom_w();
    return ScrollerCore::Interval::intersects(projected_x, right, visible_box.x, visible_box.x + visible_box.w);
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
std::string summarize_stacks(List<Stack *>& stacks) {
    std::ostringstream out;
    for (auto col = stacks.first(); col != nullptr; col = col->next()) {
        if (col != stacks.first())
            out << " | ";

        auto *data = col->data();
        out << active_window_ptr(data)
            << "@x=" << (data ? data->get_geom_x() : 0.0)
            << ",w=" << (data ? data->get_geom_w() : 0.0);
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
        column->scale(projection.min, start, projection.scale, gap);
    }
}

// Restore normal geometry after overview mode ends.
void restore_projection(List<Stack *>& stacks, ListNode<Stack *> *active, const ScrollerCore::Box &visible_box) {
    for (auto stack = stacks.first(); stack != nullptr; stack = stack->next())
        stack->data()->pop_geom();

    Stack *activeStack = active->data();
    if (activeStack->get_geom_x() < visible_box.x) {
        activeStack->set_geom_pos(visible_box.x, visible_box.y);
    } else if (activeStack->get_geom_x() + activeStack->get_geom_w() > visible_box.x + visible_box.w) {
        activeStack->set_geom_pos(visible_box.x + visible_box.w - activeStack->get_geom_w(), visible_box.y);
    }
}
} // namespace overview

namespace recalc {
// Initialize active-stack geometry when a stack is placed for the first time.
double initialize_active_stack_geometry(ListNode<Stack *> *active, const ScrollerCore::Box &visible_box, double active_width) {
    if (active->data()->get_init())
        return active->data()->get_geom_x();

    double active_x;
    if (active->prev()) {
        Stack *prev = active->prev()->data();
        active_x = prev->get_geom_x() + prev->get_geom_w();
    } else if (active->next()) {
        active_x = active->data()->get_geom_x();
    } else {
        active_x = visible_box.x + 0.5 * (visible_box.w - active_width);
    }

    active->data()->set_init();
    return active_x;
}
} // namespace recalc
} // namespace

// Compute the left/right gap pair a stack should use based on its neighbors.
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
    if (mode == Mode::Column)
        return Vector2D(max.w, 0.5 * max.h);

    return Vector2D(0.5 * max.w, max.h);
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
        stack->fit_size(FitSize::All, calculate_gap_x(active), gap);
        stack->recalculate_stack_geometry(calculate_gap_x(active), gap);
        spdlog::debug("lane_recalc_single: active_window={} stacks={}",
                      logging::active_window_ptr(stack),
                      logging::summarize_stacks(stacks));
        return;
    }

    auto a_w = active->data()->get_geom_w();
    auto a_x = recalc::initialize_active_stack_geometry(active, max, a_w);
    spdlog::debug("lane_recalc_input: active_window={} active_x={} active_w={} max=({}, {}, {}, {}) stacks_before={}",
                  logging::active_window_ptr(active->data()),
                  a_x,
                  a_w,
                  max.x,
                  max.y,
                  max.w,
                  max.h,
                  logging::summarize_stacks(stacks));
    if (a_x < max.x) {
        a_x = max.x;
        active->data()->set_geom_pos(max.x, max.y);
        adjust_stacks(active);
        spdlog::debug("lane_recalc_clamp_left: active_window={} active_x={} stacks_after={}",
                      logging::active_window_ptr(active->data()),
                      active->data()->get_geom_x(),
                      logging::summarize_stacks(stacks));
        return;
    }
    if (std::round(a_x + a_w) > max.x + max.w) {
        a_x = max.x + max.w - a_w;
        active->data()->set_geom_pos(a_x, max.y);
        adjust_stacks(active);
        spdlog::debug("lane_recalc_clamp_right: active_window={} active_x={} stacks_after={}",
                      logging::active_window_ptr(active->data()),
                      active->data()->get_geom_x(),
                      logging::summarize_stacks(stacks));
        return;
    }
    if (reorder != Reorder::Auto) {
        active->data()->set_geom_pos(a_x, max.y);
        adjust_stacks(active);
        spdlog::debug("lane_recalc_lazy: active_window={} active_x={} stacks_after={}",
                      logging::active_window_ptr(active->data()),
                      active->data()->get_geom_x(),
                      logging::summarize_stacks(stacks));
        return;
    }

    const Box active_window(max.x, max.y, max.w, max.h);
    const auto *prev = active->prev() ? active->prev()->data() : nullptr;
    const auto *next = active->next() ? active->next()->data() : nullptr;
    const auto prev_x = prev ? a_x - prev->get_geom_w() : 0.0;
    const auto next_x = a_x + a_w;
    const bool prev_inside = viewport::projected_stack_intersects_visible_box(prev, prev_x, active_window);
    const bool next_inside = viewport::projected_stack_intersects_visible_box(next, next_x, active_window);
    const bool keep_current = prev_inside || next_inside;
    const auto prev_width = prev ? prev->get_geom_w() : 0.0;
    const auto next_width = next ? next->get_geom_w() : 0.0;
    const double new_x = keep_current
        ? a_x
        : ScrollerCore::choose_anchor_x(next != nullptr, prev != nullptr, a_w, next_width, prev_width, a_x, max);
    active->data()->set_geom_pos(new_x, max.y);
    adjust_stacks(active);
    spdlog::debug("lane_recalc_auto: active_window={} keep_current={} prev_inside={} next_inside={} new_x={} stacks_after={}",
                  logging::active_window_ptr(active->data()),
                  keep_current,
                  prev_inside,
                  next_inside,
                  new_x,
                  logging::summarize_stacks(stacks));
}

void Lane::adjust_stacks(ListNode<Stack *> *stack) {
    for (auto col = stack->prev(), prev = stack; col != nullptr; prev = col, col = col->prev()) {
        col->data()->set_geom_pos(prev->data()->get_geom_x() - col->data()->get_geom_w(), max.y);
        col->data()->set_init();
    }
    for (auto col = stack->next(), prev = stack; col != nullptr; prev = col, col = col->next()) {
        col->data()->set_geom_pos(prev->data()->get_geom_x() + prev->data()->get_geom_w(), max.y);
        col->data()->set_init();
    }

    stack->data()->set_init();

    for (auto col = stacks.first(); col != nullptr; col = col->next()) {
        auto gap0 = col == stacks.first() ? 0.0 : gap;
        auto gap1 = col == stacks.last() ? 0.0 : gap;
        col->data()->recalculate_stack_geometry(Vector2D(gap0, gap1), gap);
    }
}
