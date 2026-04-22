/**
 * @file stack_geometry.cpp
 * @brief Stack geometry transforms, relayout, and resize logic.
 *
 * A Stack owns one viewport-sized strip inside a lane. The functions here keep
 * that strip internally consistent: they rescale child windows, clamp the
 * active window back into view, and translate stack-local coordinates into
 * actual Hyprland window position/size fields.
 */
#include "model/stack/internal.h"

#include <algorithm>
#include <cmath>

#include <hyprland/src/Compositor.hpp>
#include <spdlog/spdlog.h>

#include "core/fit_size.h"
#include "core/layout_math.h"
#include "core/layout_profile.h"
#include "core/monitor_geometry_runtime.h"

namespace ScrollerModel {
namespace {

using ScrollerCore::Box;

Box logical_window_box(const Box &stackGeom, Mode mode, double border, const Vector2D &gap_x,
                       double localPos, double localSize, double gap0, double gap1) {
    const auto position = StackInternal::compose_window_position(stackGeom, mode, border, gap_x, localPos, gap0);
    const auto size = StackInternal::compose_window_size(stackGeom, mode, border, gap_x, localSize, gap0, gap1);
    return {
        position.x,
        position.y,
        size.x,
        size.y,
    };
}

Box safe_committed_box(const Box &logicalBox, const Box &visibleBox, double minSpan) {
    auto safeBox = logicalBox;
    safeBox.w = std::max(safeBox.w, minSpan);
    safeBox.h = std::max(safeBox.h, minSpan);

    const auto visibleRight = visibleBox.x + visibleBox.w;
    const auto visibleBottom = visibleBox.y + visibleBox.h;
    const auto boxRight = logicalBox.x + logicalBox.w;
    const auto boxBottom = logicalBox.y + logicalBox.h;
    const bool intersectsVisible =
        boxRight > visibleBox.x &&
        logicalBox.x < visibleRight &&
        boxBottom > visibleBox.y &&
        logicalBox.y < visibleBottom;

    if (intersectsVisible)
        return safeBox;

    if (boxRight <= visibleBox.x) {
        safeBox.x = visibleBox.x - safeBox.w;
        return safeBox;
    }

    if (logicalBox.x >= visibleRight) {
        safeBox.x = visibleRight;
        return safeBox;
    }

    if (boxBottom <= visibleBox.y) {
        safeBox.y = visibleBox.y - safeBox.h;
        return safeBox;
    }

    if (logicalBox.y >= visibleBottom) {
        safeBox.y = visibleBottom;
        return safeBox;
    }

    return safeBox;
}

} // namespace

std::vector<WindowGeometryEntry> Stack::capture_window_boxes(const Vector2D &gap_x, double gap) const {
    std::vector<WindowGeometryEntry> boxes;
    for (auto win = windows.first(); win != nullptr; win = win->next()) {
        const auto window = win->data()->ptr().lock();
        if (!window)
            continue;

        const auto gap0 = win == windows.first() ? 0.0 : gap;
        const auto gap1 = win == windows.last() ? 0.0 : gap;
        const auto border = window->getRealBorderSize();
        boxes.push_back({
            .window = window,
            .box = logical_window_box(geom, mode, border, gap_x, win->data()->get_geom_y(), win->data()->get_geom_h(), gap0, gap1),
        });
    }
    return boxes;
}

void Stack::scale(const Vector2D &bmin, const Vector2D &start, double scale, double gap) {
    for (auto win = windows.first(); win != nullptr; win = win->next()) {
        // Window geometry is stored in stack-local coordinates. Rescaling the
        // stack therefore means rescaling each child's local offset and span
        // first, then rebuilding compositor coordinates from that local state.
        const auto oldLocal = win->data()->get_geom_y();
        const auto newLocal = mode == Mode::Column
            ? start.x + (oldLocal - bmin.x) * scale
            : start.y + (oldLocal - bmin.y) * scale;
        win->data()->set_geom_y(newLocal);
        win->data()->set_geom_h(win->data()->get_geom_h() * scale);
        PHLWINDOW window = win->data()->ptr().lock();
        if (!window)
            continue;
        auto border = window->getRealBorderSize();
        auto gap0 = win == windows.first() ? 0.0 : gap;
        auto gap1 = win == windows.last() ? 0.0 : gap;
        if (mode == Mode::Column) {
            window->m_position = Vector2D(win->data()->get_geom_y() + border + gap0,
                                          start.y + border + geom.y - bmin.y);
            window->m_size.x = (window->m_size.x + 2.0 * border + gap0 + gap1) * scale - gap0 - gap1 - 2.0 * border;
            window->m_size.y *= scale;
        } else {
            window->m_position = Vector2D(start.x + border + geom.x - bmin.x,
                                          win->data()->get_geom_y() + border + gap0);
            window->m_size.x *= scale;
            window->m_size.y = (window->m_size.y + 2.0 * border + gap0 + gap1) * scale - gap0 - gap1 - 2.0 * border;
        }
        window->m_size = Vector2D(std::max(window->m_size.x, 1.0), std::max(window->m_size.y, 1.0));
        StackInternal::sync_window_target_geometry(window);
    }
}

bool Stack::toggle_fullscreen(const ScrollerCore::Box &fullbbox) {
    full = fullbbox;
    if (!active)
        return false;

    // Only the stack box itself is swapped here. The actual active-window
    // compositor geometry will be refreshed by recalculate_stack_geometry().
    fullscreened = !fullscreened;
    if (fullscreened) {
        mem.geom = geom;
        if (mode == Mode::Column)
            geom.h = fullbbox.h;
        else
            geom.w = fullbbox.w;
    } else {
        geom = mem.geom;
    }
    return fullscreened;
}

void Stack::set_fullscreen(const ScrollerCore::Box &fullbbox) {
    full = fullbbox;
}

void Stack::push_geom() {
    // Save both the stack frame and every child window so temporary states like
    // maximize can be reversed without reconstructing geometry from scratch.
    mem.geom = geom;
    for (auto w = windows.first(); w != nullptr; w = w->next())
        w->data()->push_geom();
}

void Stack::pop_geom() {
    geom = mem.geom;
    for (auto w = windows.first(); w != nullptr; w = w->next())
        w->data()->pop_geom();
}

void Stack::toggle_maximized(double maxw, double maxh) {
    if (!active)
        return;

    // "Maximized" here means: expand the stack along the lane axis and expand
    // the active window along the cross axis, while preserving enough state to
    // restore the previous free-form geometry later.
    maxdim = !maxdim;
    if (maxdim) {
        mem.geom = geom;
        active->data()->push_geom();
        if (mode == Mode::Column) {
            geom.h = maxh;
            active->data()->set_geom_h(maxw);
        } else {
            geom.w = maxw;
            active->data()->set_geom_h(maxh);
        }
    } else {
        geom = mem.geom;
        active->data()->pop_geom();
    }
}

void Stack::recalculate_stack_geometry(const Vector2D &gap_x, double gap, const ScrollerCore::Box &visibleBox) {
    if (!active)
        return;

    // Scroller-managed fullscreen is a hard override: only the active window is
    // shown and it should exactly match the fullscreen box.
    if (fullscreen()) {
        PHLWINDOW activeWindow = active->data()->ptr().lock();
        if (!activeWindow)
            return;
        active->data()->set_geom_y(StackInternal::stack_local_origin(full, mode));
        activeWindow->m_position = Vector2D(full.x, full.y);
        activeWindow->m_size = Vector2D(full.w, full.h);
        StackInternal::sync_window_target_geometry(activeWindow);
        return;
    }

    Window *wactive = active->data();
    PHLWINDOW win = wactive->ptr().lock();
    if (!win)
        return;
    const auto viewportStart = StackInternal::stack_local_origin(geom, mode);
    const auto viewportEnd = StackInternal::local_viewport_end(geom, mode);
    const auto clampEnd = viewportEnd - wactive->get_geom_h();
    const auto a0 = std::round(wactive->get_geom_y());
    const auto a1 = std::round(wactive->get_geom_y() + wactive->get_geom_h());

    spdlog::debug("stack_recalc_input: active_window={} logical_pos={} logical_span={} geom=({}, {}, {}, {}) mode={}",
                  static_cast<const void*>(win ? win.get() : nullptr),
                  wactive->get_geom_y(),
                  wactive->get_geom_h(),
                  geom.x,
                  geom.y,
                  geom.w,
                  geom.h,
                  mode == Mode::Column ? "column" : "row");

    // Clamp first when the active window has drifted outside the stack viewport.
    // This catches direct resizes or moves before any reordering heuristics run.
    if (a0 < viewportStart) {
        wactive->set_geom_y(viewportStart);
        adjust_windows(active, gap_x, gap, visibleBox);
        return;
    }
    if (a1 > viewportEnd) {
        wactive->set_geom_y(clampEnd);
        adjust_windows(active, gap_x, gap, visibleBox);
        return;
    }
    // Lazy reorder means the caller deliberately positioned the active window;
    // we only need to propagate that anchor to neighbors.
    if (reorder != Reorder::Auto) {
        adjust_windows(active, gap_x, gap, visibleBox);
        return;
    }

    Window *prev = active->prev() ? active->prev()->data() : nullptr;
    Window *next = active->next() ? active->next()->data() : nullptr;
    const bool prevVisible = StackInternal::is_window_fully_visible(prev, geom, mode);
    const bool nextVisible = StackInternal::is_window_fully_visible(next, geom, mode);
    // When at least one neighbor is still fully visible, keeping the current
    // anchor avoids unnecessary jumps while navigating within a stack.
    if (prevVisible || nextVisible) {
        adjust_windows(active, gap_x, gap, visibleBox);
        return;
    }

    const auto nextSize = next ? next->get_geom_h() : 0.0;
    const auto prevSize = prev ? prev->get_geom_h() : 0.0;
    const auto activeSize = wactive->get_geom_h();
    // If both adjacent windows fell out of view, choose a fresh anchor that
    // brings the active window back to a readable position in the viewport.
    const double newPos =
        mode == Mode::Column
            ? ScrollerCore::choose_anchor_x(next != nullptr, prev != nullptr, activeSize, nextSize, prevSize, wactive->get_geom_y(), geom)
            : ScrollerCore::choose_anchor_y(next != nullptr, prev != nullptr, activeSize, nextSize, prevSize, geom);
    wactive->set_geom_y(newPos);
    adjust_windows(active, gap_x, gap, visibleBox);
    spdlog::debug("stack_recalc_auto: active_window={} prev_visible={} next_visible={} new_pos={}",
                  static_cast<const void*>(win ? win.get() : nullptr),
                  prevVisible,
                  nextVisible,
                  newPos);
}

void Stack::fit_size(FitSize fitsize, const Vector2D &gap_x, double gap, const ScrollerCore::Box &visibleBox) {
    reorder = Reorder::Auto;
    // Fit-size acts on a contiguous range of windows. The helpers decide which
    // range should be scaled (all / visible / before / after active), then
    // normalize that range so the chosen windows exactly fill the stack span.
    const auto [from, to] = ScrollerCore::select_fit_size_range(
        fitsize,
        windows.first(),
        windows.last(),
        active,
        [&](ListNode<Window *> *node) {
            return node && StackInternal::is_window_intersect_viewport(node->data(), geom, mode);
        });
    if (!ScrollerCore::normalize_fit_size_range(
            from,
            to,
            StackInternal::stack_local_span(geom, mode),
            [](ListNode<Window *> *node) {
                return node->data()->get_geom_h();
            },
            [](ListNode<Window *> *node, double scaledSpan) {
                auto *window = node->data();
                window->set_height_free();
                window->set_geom_h(scaledSpan);
            }))
        return;

    from->data()->set_geom_y(StackInternal::stack_local_origin(geom, mode));
    adjust_windows(from, gap_x, gap, visibleBox);
}

void Stack::adjust_windows(ListNode<Window *> *win, const Vector2D &gap_x, double gap, const ScrollerCore::Box &visibleBox) {
    if (!win)
        return;

    // First rebuild pure local ordering around the anchor window. At this point
    // we still operate in stack-local coordinates, not compositor coordinates.
    for (auto w = win->prev(), p = win; w != nullptr; p = w, w = w->prev()) {
        auto *wdata = w->data();
        auto *pdata = p->data();
        wdata->set_geom_y(pdata->get_geom_y() - wdata->get_geom_h());
    }
    for (auto w = win->next(), p = win; w != nullptr; p = w, w = w->next()) {
        auto *wdata = w->data();
        auto *pdata = p->data();
        wdata->set_geom_y(pdata->get_geom_y() + pdata->get_geom_h());
    }

    auto anchorWindow = win ? win->data()->ptr().lock() : nullptr;
    auto monitor = anchorWindow ? g_pCompositor->getMonitorFromID(anchorWindow->monitorID()) : nullptr;
    const auto monitorBox = monitor ? ScrollerCore::logical_monitor_box(monitor) : ScrollerCore::Box{};
    const auto fullStart = monitor ? (mode == Mode::Column ? monitorBox.x : monitorBox.y) : StackInternal::stack_local_origin(geom, mode);
    const auto fullEnd = monitor
        ? (mode == Mode::Column ? monitorBox.x + monitorBox.w : monitorBox.y + monitorBox.h)
        : StackInternal::local_viewport_end(geom, mode);
    const auto reservedBefore = std::max(0.0, StackInternal::stack_local_origin(geom, mode) - fullStart);
    const auto reservedAfter = std::max(0.0, fullEnd - StackInternal::local_viewport_end(geom, mode));

    // Keep fully clipped windows offset by the monitor reserved areas instead
    // of repeatedly snapping them back toward the visible viewport.
    size_t shiftedAbove = 0;
    size_t shiftedBelow = 0;
    for (auto w = windows.first(); w != nullptr; w = w->next()) {
        auto *wdata = w->data();
        const auto window = wdata->ptr().lock();
        const auto border = window ? window->getRealBorderSize() : 0.0;
        const auto gap0 = w == windows.first() ? 0.0 : gap;
        const auto gap1 = w == windows.last() ? 0.0 : gap;
        const auto rendered = ScrollerCore::rendered_local_interval(wdata->get_geom_y(), wdata->get_geom_h(), border, gap0, gap1);
        const auto boxStart = wdata->get_geom_y();

        if (reservedBefore > 0.0 && rendered.end <= StackInternal::stack_local_origin(geom, mode)) {
            wdata->set_geom_y(boxStart - reservedBefore);
            shiftedAbove++;
            continue;
        }

        if (reservedAfter > 0.0 && rendered.start >= StackInternal::local_viewport_end(geom, mode)) {
            wdata->set_geom_y(boxStart + reservedAfter);
            shiftedBelow++;
        }
    }

    if (shiftedAbove > 0 || shiftedBelow > 0) {
        spdlog::debug("stack_recalc_reserved_shift: active_window={} reserved_before={} reserved_after={} shifted_before={} shifted_after={}",
                      static_cast<const void*>(anchorWindow ? anchorWindow.get() : nullptr),
                      reservedBefore,
                      reservedAfter,
                      shiftedAbove,
                      shiftedBelow);
    }

    // The final pass is the "bridge" back to Hyprland. Every local window box
    // is translated into actual monitor-relative position/size and synced to the
    // compositor's animated target geometry.
    for (auto w = windows.first(); w != nullptr; w = w->next()) {
        PHLWINDOW window = w->data()->ptr().lock();
        if (!window)
            continue;
        auto gap0 = w == windows.first() ? 0.0 : gap;
        auto gap1 = w == windows.last() ? 0.0 : gap;
        auto border = window->getRealBorderSize();
        const auto localSize = w->data()->get_geom_h();
        const auto logicalBox = logical_window_box(geom, mode, border, gap_x, w->data()->get_geom_y(), localSize, gap0, gap1);
        const auto minCommittedSpan = std::max(1.0, 2.0 * border + 1.0);
        const auto committedBox = safe_committed_box(logicalBox, visibleBox, minCommittedSpan);
        window->m_position = Vector2D(committedBox.x, committedBox.y);
        window->m_size = Vector2D(committedBox.w, committedBox.h);
        StackInternal::sync_window_target_geometry(window);
    }
}

void Stack::resize_active_window(const ScrollerCore::Box &bounds, const Vector2D &gap_x, double gap, const Vector2D &delta) {
    if (!active)
        return;

    const auto activeWindow = active->data()->ptr().lock();
    if (!activeWindow)
        return;

    auto border = activeWindow->getRealBorderSize();
    // A pointer drag can resize along two logical axes at once:
    // - stackDelta changes the outer stack size inside the lane
    // - windowDelta changes the active child size inside that stack
    const auto stackDelta = mode == Mode::Column ? delta.y : delta.x;
    const auto windowDelta = mode == Mode::Column ? delta.x : delta.y;
    auto renderedStackSpan = StackInternal::stack_primary_span(geom, mode) + stackDelta - 2.0 * border - gap_x.x - gap_x.y;
    auto maxStackSpan = StackInternal::stack_primary_span(geom, mode) + stackDelta - 2.0 * (border + std::max(std::max(gap_x.x, gap_x.y), gap));
    const auto maxPrimary = ScrollerCore::stack_primary_span_limit(mode, bounds);
    if (maxStackSpan <= 0.0 || renderedStackSpan >= maxPrimary)
        return;

    // Validate the inner resize before mutating anything. Every window must stay
    // positive-sized and the active one must still fit inside the stack viewport.
    if (std::abs(static_cast<int>(windowDelta)) > 0) {
        for (auto win = windows.first(); win != nullptr; win = win->next()) {
            auto gap0 = win == windows.first() ? 0.0 : gap;
            auto gap1 = win == windows.last() ? 0.0 : gap;
            auto wh = win->data()->get_geom_h() - gap0 - gap1 - 2.0 * border;
            const auto window = win->data()->ptr().lock();
            if (!window)
                return;
            if (win == active)
                wh += windowDelta;
            if (wh <= 0.0 || wh + 2.0 * window->getRealBorderSize() + gap0 + gap1 > StackInternal::stack_local_span(geom, mode))
                return;
        }
    }
    reorder = Reorder::Auto;
    width = StackWidth::Free;

    // Resizing by drag always puts the stack into free-width mode because the
    // result no longer matches the predefined fractional width presets.
    if (mode == Mode::Column)
        geom.h += stackDelta;
    else
        geom.w += stackDelta;

    if (std::abs(static_cast<int>(windowDelta)) > 0) {
        for (auto win = windows.first(); win != nullptr; win = win->next()) {
            Window *window = win->data();
            if (win == active)
                window->set_geom_h(window->get_geom_h() + windowDelta);
        }
    }
}

} // namespace ScrollerModel
