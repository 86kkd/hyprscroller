/**
 * @file stack_core.cpp
 * @brief Stack construction, preset sizing, and basic state accessors.
 *
 * This file introduces the coordinate vocabulary used by the rest of the stack
 * implementation. The important idea for new readers is that a `Stack` reasons
 * in a stack-local axis:
 * - in row mode, the local axis is vertical (`y` / `h`)
 * - in column mode, the local axis is horizontal (`x` / `w`)
 *
 * Later files (`stack_geometry.cpp`, `stack_membership.cpp`) build on that
 * vocabulary, so understanding the helpers below makes the rest of the stack
 * code much easier to follow.
 */
#include "stack_internal.h"

#include <algorithm>
#include <cmath>
#include <string>

#include <hyprlang.hpp>
#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/config/ConfigManager.hpp>
#include <hyprland/src/config/ConfigValue.hpp>
#include <hyprland/src/layout/target/Target.hpp>
#include <hyprland/src/plugins/PluginAPI.hpp>

#include "../core/interval.h"

extern HANDLE PHANDLE;

namespace ScrollerModel::StackInternal {

// These helpers translate one `ScrollerCore::Box` into "what does this mean
// along the stack's scrolling axis versus the cross axis?" They are small, but
// they remove a huge amount of mode-dependent branching from later code.
double stack_local_origin(const ScrollerCore::Box &geom, Mode mode) {
    return mode == Mode::Column ? geom.x : geom.y;
}

double stack_local_span(const ScrollerCore::Box &geom, Mode mode) {
    return mode == Mode::Column ? geom.w : geom.h;
}

double stack_cross_span(const ScrollerCore::Box &geom, Mode mode) {
    return mode == Mode::Column ? geom.h : geom.w;
}

double stack_primary_span(const ScrollerCore::Box &geom, Mode mode) {
    return mode == Mode::Column ? geom.h : geom.w;
}

double local_viewport_end(const ScrollerCore::Box &geom, Mode mode) {
    return stack_local_origin(geom, mode) + stack_local_span(geom, mode);
}

// Turn a logical geometry request back into compositor-space coordinates. The
// caller passes values in stack-local vocabulary (`local_pos`, `local_size`);
// these helpers remap them onto x/y/w/h depending on the current mode.
Vector2D compose_window_position(const ScrollerCore::Box &geom, Mode mode, double border,
                                 const Vector2D &cross_gap, double local_pos, double local_gap) {
    if (mode == Mode::Column)
        return Vector2D(local_pos + border + local_gap, geom.y + border + cross_gap.x);

    return Vector2D(geom.x + border + cross_gap.x, local_pos + border + local_gap);
}

Vector2D compose_window_size(const ScrollerCore::Box &geom, Mode mode, double border,
                             const Vector2D &cross_gap, double local_size,
                             double local_gap0, double local_gap1) {
    const auto cross_size = std::max(stack_cross_span(geom, mode) - 2.0 * border - cross_gap.x - cross_gap.y, 1.0);
    const auto main_size = std::max(local_size - 2.0 * border - local_gap0 - local_gap1, 1.0);
    if (mode == Mode::Column)
        return Vector2D(main_size, cross_size);

    return Vector2D(cross_size, main_size);
}

// Convert width presets such as one-half or two-thirds into a concrete span on
// the active primary axis.
double preset_extent(StackWidth width, double max) {
    switch (width) {
    case StackWidth::OneThird:
        return max / 3.0;
    case StackWidth::OneHalf:
        return max / 2.0;
    case StackWidth::TwoThirds:
        return 2.0 * max / 3.0;
    case StackWidth::Free:
        return max;
    default:
        return max;
    }
}

StackWidthPreset parse_stack_width_preset(PHLWINDOW window, double fallback_maxw) {
    static auto const *column_default_width =
        (Hyprlang::STRING const *)HyprlandAPI::getConfigValue(PHANDLE, "plugin:scroller:column_default_width")->getDataStaticPtr();

    // The "floating" preset means "seed the stack from the window's last known
    // floating size when possible". Every other preset maps directly to one of
    // the proportional width enums.
    const std::string preset = *column_default_width;
    if (preset == "onehalf")
        return {.width = StackWidth::OneHalf, .maxw = fallback_maxw};
    if (preset == "onethird")
        return {.width = StackWidth::OneThird, .maxw = fallback_maxw};
    if (preset == "twothirds")
        return {.width = StackWidth::TwoThirds, .maxw = fallback_maxw};
    if (preset == "maximized")
        return {.width = StackWidth::Free, .maxw = fallback_maxw};
    if (preset != "floating")
        return {.width = StackWidth::OneHalf, .maxw = fallback_maxw};

    auto target = window ? window->layoutTarget() : nullptr;
    if (target && target->lastFloatingSize().y > 0)
        return {.width = StackWidth::Free, .maxw = target->lastFloatingSize().x};

    return {.width = StackWidth::OneHalf, .maxw = fallback_maxw};
}

bool is_window_fully_visible(Window *window, const ScrollerCore::Box &geom, Mode mode) {
    if (!window)
        return false;

    const auto p0 = std::round(window->get_geom_y());
    const auto p1 = std::round(window->get_geom_y() + window->get_geom_h());
    return ScrollerCore::Interval::fully_visible(p0, p1, stack_local_origin(geom, mode), local_viewport_end(geom, mode));
}

bool is_window_intersect_viewport(Window *window, const ScrollerCore::Box &geom, Mode mode) {
    if (!window)
        return false;

    const auto p0 = window->get_geom_y();
    const auto p1 = window->get_geom_y() + window->get_geom_h();
    return ScrollerCore::Interval::intersects(p0, p1, stack_local_origin(geom, mode), local_viewport_end(geom, mode));
}

// Keep Hyprland's layout target in sync after stack code mutates the logical
// model geometry. Stack/lane code treats the model as authoritative, then
// mirrors the final result back into the compositor target.
void sync_window_target_geometry(PHLWINDOW window) {
    if (!window)
        return;

    const auto target = window->layoutTarget();
    if (!target)
        return;

    target->setPositionGlobal(Hyprutils::Math::CBox(window->m_position, window->m_size));
}

} // namespace ScrollerModel::StackInternal

namespace ScrollerModel {

Stack::Stack(PHLWINDOW cwindow, double maxw, double maxh, Mode mode)
    : mode(mode), height(WindowHeight::One), reorder(Reorder::Auto), initialized(false), maxdim(false) {
    // New stacks start from the configured default width preset, then create one
    // model window whose local-axis span matches the stack's current mode.
    const auto preset = StackInternal::parse_stack_width_preset(cwindow, maxw);
    width = preset.width;
    maxw = preset.maxw;

    Window *window = new Window(cwindow, mode == Mode::Column ? maxw : maxh, mode);
    update_width(width, maxw, maxh);
    windows.push_back(window);
    active = windows.first();
}

Stack::Stack(std::unique_ptr<Window> window, StackWidth width, double maxw, double maxh, Mode mode)
    : width(width), mode(mode), height(WindowHeight::One), reorder(Reorder::Auto), initialized(true), maxdim(false) {
    // This constructor is used when a window model is already detached from a
    // source stack and must be rebuilt into a fresh one-window destination stack.
    update_width(width, maxw, maxh);
    if (!window)
        return;

    window->set_geom_h(StackInternal::stack_local_span(geom, mode));
    windows.push_back(window.release());
    active = windows.first();
}

Stack::~Stack() {
    for (auto win = windows.first(); win != nullptr; win = win->next())
        delete win->data();
    windows.clear();
}

bool Stack::get_init() const {
    return initialized;
}

void Stack::set_init() {
    initialized = true;
}

size_t Stack::size() const {
    return windows.size();
}

double Stack::get_geom_x() const {
    return geom.x;
}

double Stack::get_geom_y() const {
    return geom.y;
}

double Stack::get_geom_w() const {
    return geom.w;
}

double Stack::get_geom_h() const {
    return geom.h;
}

void Stack::set_geom_w(double w) {
    geom.w = w;
}

void Stack::set_geom_h(double h) {
    geom.h = h;
}

Vector2D Stack::get_height() const {
    if (windows.empty())
        return Vector2D(geom.y, geom.y);

    if (mode == Mode::Column)
        return Vector2D(geom.y, geom.y + geom.h);

    Vector2D height;
    auto *first = windows.first()->data();
    auto *last = windows.last()->data();
    height.x = first->get_geom_y();
    height.y = last->get_geom_y() + last->get_geom_h();
    return height;
}

bool Stack::fullscreen() const {
    if (!active)
        return false;

    auto window = active->data()->ptr().lock();
    return window ? window->isFullscreen() : false;
}

bool Stack::expanded() const {
    return fullscreened;
}

bool Stack::maximized() const {
    return maxdim;
}

Mode Stack::get_mode() const {
    return mode;
}

void Stack::set_mode(Mode nextMode, double maxw, double maxh) {
    if (mode == nextMode)
        return;

    // Mode flips reinterpret which axis is local. Recompute stack bounds for the
    // new orientation, then rewrite the single active window when the stack only
    // owns one item so that its local coordinates still span the whole stack.
    mode = nextMode;
    update_width(width, maxw, maxh);
    if (active && size() == 1) {
        active->data()->set_geom_h(StackInternal::stack_local_span(geom, mode));
        active->data()->set_geom_y(StackInternal::stack_local_origin(geom, mode));
    }
}

void Stack::shift_local_geometry(double delta) {
    if (delta == 0.0)
        return;

    for (auto win = windows.first(); win != nullptr; win = win->next()) {
        auto *window = win->data();
        window->set_geom_y(window->get_geom_y() + delta);
    }
}

void Stack::set_geom_pos(double x, double y) {
    geom.set_pos(x, y);
}

StackWidth Stack::get_width() const {
    return width;
}

void Stack::set_width_free() {
    width = StackWidth::Free;
}

#ifdef COLORS_IPC
std::string Stack::get_width_name() const {
    switch (width) {
    case StackWidth::OneThird:
        return "OneThird";
    case StackWidth::OneHalf:
        return "OneHalf";
    case StackWidth::TwoThirds:
        return "TwoThirds";
    case StackWidth::Free:
        return "Free";
    default:
        return "";
    }
}

std::string Stack::get_height_name() const {
    switch (height) {
    case WindowHeight::Auto:
        return "Auto";
    case WindowHeight::Free:
        return "Free";
    default:
        return "";
    }
}
#endif

void Stack::update_width(StackWidth cwidth, double maxw, double maxh) {
    // Width presets apply to the stack's primary axis. In column mode that axis
    // is height-like (`geom.h`); in row mode it is width-like (`geom.w`).
    if (mode == Mode::Column) {
        geom.w = maxw;
        geom.h = maximized() ? maxh : StackInternal::preset_extent(cwidth, maxh);
    } else {
        geom.w = maximized() ? maxw : StackInternal::preset_extent(cwidth, maxw);
        geom.h = maxh;
    }
    width = cwidth;
}

} // namespace ScrollerModel
