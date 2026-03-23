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

#include "../canvas/internal.h"

namespace {
namespace viewport {
bool stack_intersects_visible_box(const Stack *stack, const ScrollerCore::Box &visible_box) {
    if (!stack)
        return false;

    const auto left = stack->get_geom_x();
    const auto right = left + stack->get_geom_w();
    return left < visible_box.x + visible_box.w && left >= visible_box.x ||
           right > visible_box.x && right <= visible_box.x + visible_box.w ||
           left < visible_box.x && right >= visible_box.x + visible_box.w;
}

double choose_anchor_x(const ListNode<Stack *> *active, const double active_width,
                       const double fallback_x, const ScrollerCore::Box &visible_box) {
    const auto next = active->next();
    const auto prev = active->prev();
    if (next) {
        const auto next_width = next->data()->get_geom_w();
        if (active_width + next_width <= visible_box.w)
            return visible_box.x + visible_box.w - active_width - next_width;
        if (prev && prev->data()->get_geom_w() + active_width <= visible_box.w)
            return visible_box.x + prev->data()->get_geom_w();
        if (!prev)
            return visible_box.x;
        return fallback_x;
    }
    if (prev) {
        if (prev->data()->get_geom_w() + active_width <= visible_box.w)
            return visible_box.x + prev->data()->get_geom_w();
        return visible_box.x + visible_box.w - active_width;
    }
    return fallback_x;
}
} // namespace viewport

namespace logging {
const void* active_window_ptr(Stack *stack) {
    if (!stack)
        return nullptr;

    const auto window = stack->get_active_window();
    return static_cast<const void*>(window ? window.get() : nullptr);
}

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
struct Projection {
    Vector2D min;
    Vector2D max;
    double   width;
    double   height;
    double   scale;
    Vector2D offset;
};

Projection compute_projection(List<Stack *>& stacks, const ScrollerCore::Box &visible_box) {
    Vector2D bmin(visible_box.x + visible_box.w, visible_box.y + visible_box.h);
    Vector2D bmax(visible_box.x, visible_box.y);
    for (auto stack = stacks.first(); stack != nullptr; stack = stack->next()) {
        auto x0 = stack->data()->get_geom_x();
        auto x1 = x0 + stack->data()->get_geom_w();
        Vector2D height = stack->data()->get_height();
        if (x0 < bmin.x)
            bmin.x = x0;
        if (x1 > bmax.x)
            bmax.x = x1;
        if (height.x < bmin.y)
            bmin.y = height.x;
        if (height.y > bmax.y)
            bmax.y = height.y;
    }

    const auto width = bmax.x - bmin.x;
    const auto height = bmax.y - bmin.y;
    const auto scale = std::min(visible_box.w / width, visible_box.h / height);
    const auto offset = Vector2D(0.5 * (visible_box.w - width * scale), 0.5 * (visible_box.h - height * scale));
    return Projection{bmin, bmax, width, height, scale, offset};
}

void apply_projection(List<Stack *>& stacks, const Projection &projection, double gap, const ScrollerCore::Box &visible_box) {
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

Vector2D Lane::calculate_gap_x(const ListNode<Stack *> *stack) const {
    auto gap0 = stack == stacks.first() ? 0.0 : gap;
    auto gap1 = stack == stacks.last() ? 0.0 : gap;
    return Vector2D(gap0, gap1);
}

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

Vector2D Lane::predict_window_size() const {
    if (mode == Mode::Column)
        return Vector2D(max.w, 0.5 * max.h);

    return Vector2D(0.5 * max.w, max.h);
}

void Lane::update_sizes(PHLMONITOR monitor) {
    if (!monitor)
        return;

    const auto bounds = CanvasLayoutInternal::compute_canvas_bounds(monitor);
    full = bounds.full;
    max = bounds.max;
    gap = bounds.gap;
}

void Lane::set_fullscreen_active_window() {
    if (!active)
        return;

    active->data()->set_fullscreen(full);
    active->data()->recalculate_stack_geometry(calculate_gap_x(active), gap);
}

void Lane::toggle_fullscreen_active_window() {
    if (!active)
        return;

    Stack *stack = active->data();
    (void)stack->toggle_fullscreen(max, mode);
    recalculate_lane_geometry();
}

void Lane::toggle_maximize_active_stack() {
    if (!active)
        return;

    Stack *stack = active->data();
    stack->toggle_maximized(max.w, max.h);
    reorder = Reorder::Auto;
    recalculate_lane_geometry();
}

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
    if (active->data()->get_width() == StackWidth::Free) {
        active->data()->get_active_window()->m_cRealBorderColor = *FREECOLUMN;
    } else {
        active->data()->get_active_window()->m_cRealBorderColor = *ACTIVECOL;
    }
    g_pEventManager->postEvent(SHyprIPCEvent{"scroller", active->data()->get_width_name() + "," + active->data()->get_height_name()});
#endif
    if (stacks.size() == 1 && active->data()->size() == 1) {
        active->data()->set_geom_pos(max.x, max.y);
        active->data()->set_geom_w(max.w);
        active->data()->recalculate_stack_geometry(calculate_gap_x(active), gap);
        spdlog::debug("lane_recalc_single: active_window={} stacks={}",
                      logging::active_window_ptr(active->data()),
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
    const bool prev_inside = viewport::stack_intersects_visible_box(active->prev() ? active->prev()->data() : nullptr, active_window);
    const bool next_inside = viewport::stack_intersects_visible_box(active->next() ? active->next()->data() : nullptr, active_window);
    const bool keep_current = prev_inside || next_inside;
    const double new_x = keep_current ? a_x : viewport::choose_anchor_x(active, a_w, a_x, max);
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
