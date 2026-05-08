/**
 * @file plugin/dispatch/layout_commands.cpp
 * @brief Layout-oriented dispatcher entrypoints and registration.
 */
#include "plugin/dispatch/shared.h"

#include "core/core.h"

namespace dispatchers::detail {
namespace {

// cyclesize(+1|-1): change active stack width/height step.
void dispatch_cyclesize(std::string arg) {
    int step = 0;
    if (arg == "+1" || arg == "1" || arg == "next") {
        step = 1;
    } else if (arg == "-1" || arg == "prev" || arg == "previous") {
        step = -1;
    } else {
        return;
    }

    const auto action = workspaceLayoutForAction();
    if (action) {
        action.layout->cycle_window_size(action.workspace, step);
        return;
    }

    const auto gridAction = workspaceGridLayoutForAction();
    if (gridAction)
        gridAction.layout->cycle_window_size(gridAction.workspace, step);
}

// movefocus <dir>: move focus inside scroller layout, with optional monitor
// fallback when the active lane cannot move in requested direction.
// This is the top of the focus path:
// keybind -> dispatcher parse -> active canvas -> `CanvasLayout::move_focus`.
void dispatch_movefocus(std::string arg) {
    const auto direction = parsedDirectionArg(arg);
    if (!direction) {
        spdlog::warn("dispatch_movefocus: unsupported arg='{}'", arg);
        return;
    }

    if (Overview::session().active()) {
        spdlog::info("dispatch_movefocus: overview arg='{}'", arg);
        Overview::session().markInputHandled();
        (void)Overview::session().moveSelection(*direction);
        return;
    }

    const auto action = workspaceLayoutForAction();
    if (action) {
        spdlog::info("dispatch_movefocus: arg='{}' workspace={}", arg, action.workspace);
        action.layout->move_focus(action.workspace, *direction);
        return;
    }

    const auto gridAction = workspaceGridLayoutForAction();
    if (gridAction) {
        spdlog::info("dispatch_movefocus: grid arg='{}' workspace={}", arg, gridAction.workspace);
        gridAction.layout->move_focus(gridAction.workspace, *direction);
        return;
    }

    spdlog::warn("dispatch_movefocus: no layout for arg='{}', fallback builtin", arg);
    CanvasLayoutInternal::dispatch_builtin_movefocus(*direction);
}

// focusmonitor <dir>: move monitor focus in normal mode, or move between
// canvas-workspace tiles in overview mode.
void dispatch_focusmonitor(std::string arg) {
    const auto direction = parsedDirectionArg(arg);
    if (!direction) {
        spdlog::warn("dispatch_focusmonitor: unsupported arg='{}'", arg);
        return;
    }

    if (Overview::session().active()) {
        spdlog::info("dispatch_focusmonitor: overview arg='{}'", arg);
        overviewSessionForDispatch().moveCanvasSelection(*direction);
        return;
    }

    const auto sourceMonitor = ScrollerCore::monitorFromPointingOrCursor();
    const auto monitorDirection = CanvasLayoutInternal::direction_to_math(*direction);
    if (!g_pCompositor || !sourceMonitor || !monitorDirection) {
        spdlog::warn("dispatch_focusmonitor: no route direction={} source_monitor={}",
                     ScrollerCore::direction_name(*direction),
                     sourceMonitor ? sourceMonitor->m_id : MONITOR_INVALID);
        return;
    }

    const auto targetMonitor = g_pCompositor->getMonitorInDirection(sourceMonitor, *monitorDirection);
    if (!targetMonitor) {
        spdlog::info("dispatch_focusmonitor: no adjacent monitor direction={} source_monitor={}",
                     ScrollerCore::direction_name(*direction),
                     sourceMonitor->m_id);
        return;
    }

    const auto targetWorkspaceId = CanvasLayoutInternal::preferred_workspace_id(targetMonitor, targetMonitor->activeWorkspaceID());
    const auto targetWorkspace = g_pCompositor->getWorkspaceByID(targetWorkspaceId);
    if (!CanvasLayoutInternal::focus_monitor_workspace(targetMonitor,
                                                       targetWorkspace,
                                                       targetWorkspaceId,
                                                       true,
                                                       "dispatch_focusmonitor")) {
        spdlog::warn("dispatch_focusmonitor: failed direction={} target_monitor={} workspace={}",
                     ScrollerCore::direction_name(*direction),
                     targetMonitor->m_id,
                     targetWorkspaceId);
    }
}

// movewindow <dir>: reorder active window inside lane/stack.
// This is the top of the window-move path:
// keybind -> dispatcher parse -> active canvas -> `CanvasLayout::move_window`.
void dispatch_movewindow(std::string arg) {
    const auto direction = parsedDirectionArg(arg);
    if (!direction) {
        spdlog::warn("dispatch_movewindow: unsupported arg='{}'", arg);
        return;
    }

    const auto action = workspaceLayoutForAction();
    if (action) {
        action.layout->move_window(action.workspace, *direction);
        return;
    }

    const auto gridAction = workspaceGridLayoutForAction();
    if (gridAction) {
        gridAction.layout->move_window(gridAction.workspace, *direction);
        return;
    }

    spdlog::warn("dispatch_movewindow: no layout for arg='{}'", arg);
}

// alignwindow <dir>: align active window/stack against lane/stack geometry.
void dispatch_alignwindow(std::string arg) {
    const auto direction = parsedDirectionArg(arg);
    if (!direction)
        return;

    const auto action = workspaceLayoutForAction();
    if (action) {
        action.layout->align_window(action.workspace, *direction);
        return;
    }

    const auto gridAction = workspaceGridLayoutForAction();
    if (gridAction)
        gridAction.layout->align_window(gridAction.workspace, *direction);
}

// admitwindow: split active stack and move focused window to the previous stack.
void dispatch_admitwindow(std::string arg) {
    (void)arg;
    const auto action = workspaceLayoutForAction();
    if (action) {
        action.layout->admit_window_left(action.workspace);
        return;
    }

    const auto gridAction = workspaceGridLayoutForAction();
    if (gridAction)
        gridAction.layout->admit_window_left(gridAction.workspace);
}

// expelwindow: remove focused window from current stack into a new one right
// after it.
void dispatch_expelwindow(std::string arg) {
    (void)arg;
    const auto action = workspaceLayoutForAction();
    if (action) {
        action.layout->expel_window_right(action.workspace);
        return;
    }

    const auto gridAction = workspaceGridLayoutForAction();
    if (gridAction)
        gridAction.layout->expel_window_right(gridAction.workspace);
}

// setmode row|col: switch between row mode and column mode.
void dispatch_setmode(std::string arg) {
    withParsedModeArgOrWarn("dispatch_setmode", arg, [&](Mode mode) {
        const auto action = workspaceLayoutForAction();
        if (action) {
            action.layout->set_mode(action.workspace, mode);
            return;
        }

        const auto gridAction = workspaceGridLayoutForAction();
        if (gridAction)
            gridAction.layout->set_mode(gridAction.workspace, mode);
    });
}

// fitsize <active|visible|all|toend|tobeg>: resize visible windows so they
// fit requested range.
void dispatch_fitsize(std::string arg) {
    const auto fitSize = parsedFitSizeArg(arg);
    if (!fitSize)
        return;

    const auto action = workspaceLayoutForAction();
    if (action) {
        action.layout->fit_size(action.workspace, *fitSize);
        return;
    }

    const auto gridAction = workspaceGridLayoutForAction();
    if (gridAction)
        gridAction.layout->fit_size(gridAction.workspace, *fitSize);
}

// togglefullscreen: expand the active scroller window to the monitor bounds.
void dispatch_togglefullscreen(std::string arg) {
    (void)arg;
    const auto action = workspaceLayoutForAction();
    if (action) {
        action.layout->toggle_fullscreen(action.workspace);
        return;
    }

    const auto gridAction = workspaceGridLayoutForAction();
    if (gridAction)
        gridAction.layout->toggle_fullscreen(gridAction.workspace);
}

// createlane <dir>: move the active stack into a new adjacent lane.
void dispatch_createlane(std::string arg) {
    const auto direction = parsedDirectionArg(arg);
    if (!direction)
        return;

    const auto action = workspaceLayoutForAction();
    if (action) {
        action.layout->create_lane(action.workspace, *direction);
        return;
    }

    const auto gridAction = workspaceGridLayoutForAction();
    if (gridAction)
        gridAction.layout->create_lane(gridAction.workspace, *direction);
}

// focuslane <dir>: switch the active lane inside the current canvas.
void dispatch_focuslane(std::string arg) {
    const auto direction = parsedDirectionArg(arg);
    if (!direction)
        return;

    const auto action = workspaceLayoutForAction();
    if (action) {
        action.layout->focus_lane(action.workspace, *direction);
        return;
    }

    const auto gridAction = workspaceGridLayoutForAction();
    if (gridAction)
        gridAction.layout->focus_lane(gridAction.workspace, *direction);
}

} // namespace

void registerLayoutDispatchers() {
    registerDispatcher("scroller:cyclesize", dispatch_cyclesize);
    registerDispatcher("scroller:movefocus", dispatch_movefocus);
    registerDispatcher("scroller:focusmonitor", dispatch_focusmonitor);
    registerDispatcher("scroller:movewindow", dispatch_movewindow);
    registerDispatcher("scroller:alignwindow", dispatch_alignwindow);
    registerDispatcher("scroller:admitwindow", dispatch_admitwindow);
    registerDispatcher("scroller:expelwindow", dispatch_expelwindow);
    registerDispatcher("scroller:setmode", dispatch_setmode);
    registerDispatcher("scroller:fitsize", dispatch_fitsize);
    registerDispatcher("scroller:togglefullscreen", dispatch_togglefullscreen);
    registerDispatcher("scroller:createlane", dispatch_createlane);
    registerDispatcher("scroller:focuslane", dispatch_focuslane);
}

} // namespace dispatchers::detail
