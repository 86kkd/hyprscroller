/**
 * @file geometry.cpp
 * @brief Lane geometry, viewport relayout, and fullscreen/maximize helpers.
 *
 * This file contains the geometry-heavy part of lane behavior: stack visibility
 * checks, anchor selection, fullscreen/maximize layout, and the final relayout
 * pass that keeps the active stack visible.
 */
#include "lane.h"

#include <cmath>
#include <sstream>

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
#include "../../core/workarea_pager.h"
#include "../canvas/internal.h"

using ScrollerCore::Box;
using ScrollerModel::Reorder;
using ScrollerModel::Stack;
using ScrollerModel::StackWidth;

namespace {
// Read/write helpers in this anonymous namespace keep the later relayout code
// phrased in terms of the lane "primary" axis. For column mode that axis is Y;
// for row mode it is X.
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

ScrollerCore::Box primary_interval_box(const ScrollerCore::Box &base_box, Mode mode, double start, double end) {
    auto box = base_box;
    if (mode == Mode::Column) {
        box.y = start;
        box.h = std::max(1.0, end - start);
    } else {
        box.x = start;
        box.w = std::max(1.0, end - start);
    }
    return box;
}

double hidden_reserved_primary_delta(const ScrollerCore::Box &full_box,
                                     const ScrollerCore::Box &workarea_box,
                                     Mode mode,
                                     double rendered_start,
                                     double rendered_end) {
    const auto renderedBox = primary_interval_box(workarea_box, mode, rendered_start, rendered_end);
    const auto projected = ScrollerCore::project_box_to_workarea_page(renderedBox, full_box, workarea_box);
    if (projected.visible)
        return 0.0;

    return visible_primary_origin(projected.committed, mode) - visible_primary_origin(renderedBox, mode);
}

void set_stack_primary_position(Stack *stack, Mode mode, const ScrollerCore::Box &visible_box, double primary_pos) {
    if (!stack)
        return;

    if (mode == Mode::Column)
        stack->set_geom_pos(visible_box.x, primary_pos);
    else
        stack->set_geom_pos(primary_pos, visible_box.y);
}

ScrollerCore::LocalRenderInterval stack_rendered_primary_interval(Stack *stack, Mode mode,
                                                                  double gap_before, double gap_after) {
    if (!stack)
        return {};

    const auto activeWindow = stack->get_active_window();
    const auto border = activeWindow ? activeWindow->getRealBorderSize() : 0.0;
    return ScrollerCore::rendered_local_interval(stack_primary_origin(stack, mode),
                                                 stack_primary_span(stack, mode),
                                                 border,
                                                 gap_before,
                                                 gap_after);
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

namespace recalc {
// Initialize active-stack geometry when a stack is placed for the first time.
double initialize_active_stack_geometry(ListNode<Stack *> *active, const ScrollerCore::Box &visible_box,
                                        double active_span, Mode mode) {
    if (active->data()->get_init())
        return stack_primary_origin(active->data(), mode);

    // Brand-new stacks inherit their initial anchor from the nearest neighbor if
    // one already exists. Only a single-stack lane needs synthetic centering.
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
            stack->set_geom_pos(max.x, ScrollerCore::center_span(max.y, max.h, stack->get_geom_h()));
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
        stack->set_geom_pos(ScrollerCore::center_span(max.x, max.w, stack->get_geom_w()), max.y);
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
    active->data()->recalculate_stack_geometry(calculate_gap_x(active), gap, max);
}

// Toggle scroller-managed fullscreen on the active stack and relayout.
void Lane::toggle_fullscreen_active_window() {
    if (!active)
        return;

    Stack *stack = active->data();
    (void)stack->toggle_fullscreen(max);
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

// Main geometry pass for a lane: keep the active stack visible and reposition
// neighbors around it.
void Lane::recalculate_lane_geometry() {
    if (active == nullptr)
        return;

    // Hyprland-native fullscreen bypasses the normal overview/scroller viewport
    // rules. In that mode we only refresh the active stack's child-window layout.
    if (const auto activeWindow = active->data()->get_active_window(); activeWindow && activeWindow->isFullscreen()) {
        active->data()->recalculate_stack_geometry(calculate_gap_x(active), gap, max);
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
    // Degenerate case: one stack with one window should just occupy the entire
    // lane instead of running the more general anchor-selection logic below.
    if (stacks.size() == 1 && active->data()->size() == 1) {
        auto *stack = active->data();
        stack->update_width(stack->get_width(), max.w, max.h);
        stack->set_geom_pos(max.x, max.y);
        stack->set_geom_w(max.w);
        stack->set_geom_h(max.h);
        stack->fit_size(FitSize::All, calculate_gap_x(active), gap, max);
        stack->recalculate_stack_geometry(calculate_gap_x(active), gap, max);
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
    // First clamp the active stack back into the visible viewport. This handles
    // drag/resize overflow before we think about smarter re-anchoring.
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
    // Lazy reorder means "respect the caller's explicit position" and only
    // restack neighbors around it. Auto reorder may still move the active stack.
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
    // If at least one adjacent stack still intersects the viewport we keep the
    // current anchor. Otherwise we choose a fresh anchor that pulls the active
    // stack back toward the visible center while preserving stack order.
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
    // Expand outward from the anchor stack so the linked-list order directly
    // maps to geometric order on screen.
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

    const auto viewportStart = visible_primary_origin(max, mode);
    const auto viewportEnd = visible_primary_end(max, mode);
    const auto reservedBefore = std::max(0.0, viewportStart - visible_primary_origin(full, mode));
    const auto reservedAfter = std::max(0.0, visible_primary_end(full, mode) - viewportEnd);
    size_t shiftedBefore = 0;
    size_t shiftedAfter = 0;

    // Stacks that are completely clipped by reserved monitor margins should
    // use the same full-vs-workarea projection as grid pages.
    for (auto col = stacks.first(); col != nullptr; col = col->next()) {
        auto gap0 = col == stacks.first() ? 0.0 : gap;
        auto gap1 = col == stacks.last() ? 0.0 : gap;
        const auto rendered = stack_rendered_primary_interval(col->data(), mode, gap0, gap1);
        const auto primaryPos = stack_primary_origin(col->data(), mode);
        const auto primaryDelta = hidden_reserved_primary_delta(full, max, mode, rendered.start, rendered.end);

        if (primaryDelta < 0.0) {
            set_stack_primary_position(col->data(), mode, max, primaryPos + primaryDelta);
            shiftedBefore++;
            continue;
        }

        if (primaryDelta > 0.0) {
            set_stack_primary_position(col->data(), mode, max, primaryPos + primaryDelta);
            shiftedAfter++;
        }
    }

    if (shiftedBefore > 0 || shiftedAfter > 0) {
        spdlog::debug("lane_recalc_reserved_shift: active_window={} reserved_before={} reserved_after={} shifted_before={} shifted_after={} stacks={}",
                      logging::active_window_ptr(active ? active->data() : nullptr),
                      reservedBefore,
                      reservedAfter,
                      shiftedBefore,
                      shiftedAfter,
                      logging::summarize_stacks(stacks, mode));
    }

    // Once every stack box has been placed, each stack performs its own inner
    // window relayout using the lane-relative gap pair we just established.
    for (auto col = stacks.first(); col != nullptr; col = col->next()) {
        auto gap0 = col == stacks.first() ? 0.0 : gap;
        auto gap1 = col == stacks.last() ? 0.0 : gap;
        col->data()->recalculate_stack_geometry(Vector2D(gap0, gap1), gap, max);
    }
}
