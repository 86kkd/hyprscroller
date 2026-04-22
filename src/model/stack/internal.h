#pragma once

#include "model/stack/stack.h"

namespace ScrollerModel::StackInternal {

struct StackWidthPreset {
    StackWidth width = StackWidth::OneHalf;
    double     maxw = 0.0;
};

double stack_local_origin(const ScrollerCore::Box &geom, Mode mode);
double stack_local_span(const ScrollerCore::Box &geom, Mode mode);
double stack_cross_span(const ScrollerCore::Box &geom, Mode mode);
double stack_primary_span(const ScrollerCore::Box &geom, Mode mode);
double local_viewport_end(const ScrollerCore::Box &geom, Mode mode);

Vector2D compose_window_position(const ScrollerCore::Box &geom, Mode mode, double border,
                                 const Vector2D &cross_gap, double local_pos, double local_gap);
Vector2D compose_window_size(const ScrollerCore::Box &geom, Mode mode, double border,
                             const Vector2D &cross_gap, double local_size,
                             double local_gap0, double local_gap1);

double preset_extent(StackWidth width, double max);
StackWidthPreset parse_stack_width_preset(PHLWINDOW window, double fallback_maxw);

bool is_window_fully_visible(Window *window, const ScrollerCore::Box &geom, Mode mode);
bool is_window_intersect_viewport(Window *window, const ScrollerCore::Box &geom, Mode mode);
void sync_window_target_geometry(PHLWINDOW window);

} // namespace ScrollerModel::StackInternal
