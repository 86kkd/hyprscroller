/**
 * @file plugin/dispatch/layout_commands.cpp
 * @brief Layout-oriented dispatcher entrypoints and registration.
 */
#include "plugin/dispatch/shared.h"

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

    withWorkspaceLayout([&](CanvasLayout& layout, int workspace) {
        layout.cycle_window_size(workspace, step);
    });
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
    if (!action) {
        spdlog::warn("dispatch_movefocus: no layout for arg='{}', fallback builtin", arg);
        CanvasLayoutInternal::dispatch_builtin_movefocus(*direction);
        return;
    }

    spdlog::info("dispatch_movefocus: arg='{}' workspace={}", arg, action.workspace);
    action.layout->move_focus(action.workspace, *direction);
}

// movewindow <dir>: reorder active window inside lane/stack.
// This is the top of the window-move path:
// keybind -> dispatcher parse -> active canvas -> `CanvasLayout::move_window`.
void dispatch_movewindow(std::string arg) {
    withWorkspaceDirectionArg(arg, [&](CanvasLayout& layout, int workspace, Direction direction) {
        layout.move_window(workspace, direction);
    });
}

// alignwindow <dir>: align active window/stack against lane/stack geometry.
void dispatch_alignwindow(std::string arg) {
    withWorkspaceDirectionArg(arg, [&](CanvasLayout& layout, int workspace, Direction direction) {
        layout.align_window(workspace, direction);
    });
}

// admitwindow: split active stack and move focused window to the previous stack.
void dispatch_admitwindow(std::string arg) {
    (void)arg;
    withWorkspaceLayout([&](CanvasLayout& layout, int workspace) {
        layout.admit_window_left(workspace);
    });
}

// expelwindow: remove focused window from current stack into a new one right
// after it.
void dispatch_expelwindow(std::string arg) {
    (void)arg;
    withWorkspaceLayout([&](CanvasLayout& layout, int workspace) {
        layout.expel_window_right(workspace);
    });
}

// setmode row|col: switch between row mode and column mode.
void dispatch_setmode(std::string arg) {
    withParsedModeArgOrWarn("dispatch_setmode", arg, [&](Mode mode) {
        withWorkspaceLayout([&](CanvasLayout& layout, int workspace) {
            layout.set_mode(workspace, mode);
        });
    });
}

// fitsize <active|visible|all|toend|tobeg>: resize visible windows so they
// fit requested range.
void dispatch_fitsize(std::string arg) {
    withWorkspaceLayout([&](CanvasLayout& layout, int workspace) {
        withParsedFitSizeArg(arg, [&](FitSize fitSize) {
            layout.fit_size(workspace, fitSize);
        });
    });
}

// togglefullscreen: expand the active scroller window to the monitor bounds.
void dispatch_togglefullscreen(std::string arg) {
    (void)arg;
    withWorkspaceLayout([&](CanvasLayout& layout, int workspace) {
        layout.toggle_fullscreen(workspace);
    });
}

// createlane <dir>: move the active stack into a new adjacent lane.
void dispatch_createlane(std::string arg) {
    withWorkspaceDirectionArg(arg, [&](CanvasLayout& layout, int workspace, Direction direction) {
        layout.create_lane(workspace, direction);
    });
}

// focuslane <dir>: switch the active lane inside the current canvas.
void dispatch_focuslane(std::string arg) {
    withWorkspaceDirectionArg(arg, [&](CanvasLayout& layout, int workspace, Direction direction) {
        layout.focus_lane(workspace, direction);
    });
}

} // namespace

void registerLayoutDispatchers() {
    registerDispatcher("scroller:cyclesize", dispatch_cyclesize);
    registerDispatcher("scroller:movefocus", dispatch_movefocus);
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
