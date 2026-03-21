#pragma once

#include <optional>

#include "row.h"
#include "scroller_layout.h"

namespace ScrollerLayoutInternal {
const char*                     direction_name(Direction direction);
void                            recalculate_workspace_row(Row* row, PHLMONITOR monitor, PHLWORKSPACE workspace, bool honor_fullscreen);
WORKSPACEID                     preferred_workspace_id(PHLMONITOR monitor, WORKSPACEID source_workspace_id);
PHLMONITOR                      visible_monitor_for_workspace(PHLWORKSPACE workspace);
ScrollerLayout*                 get_scroller_for_workspace(WORKSPACEID workspace_id);
std::optional<Math::eDirection> direction_to_math(Direction direction);
PHLWINDOW                       pick_cross_monitor_target_window(PHLMONITOR monitor, WORKSPACEID workspace_id, Direction direction, PHLWINDOW source_window);
void                            dispatch_builtin_movefocus(Direction direction);
void                            focus_window_monitor(PHLWINDOW window);
void                            switch_to_window(PHLWINDOW window, bool warp_cursor = false);
int                             get_workspace_id();
}
