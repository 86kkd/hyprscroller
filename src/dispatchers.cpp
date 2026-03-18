#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/includes.hpp>
#include <hyprlang.hpp>
#include <hyprland/src/plugins/PluginAPI.hpp>
#include <hyprutils/string/VarList.hpp>
#include <hyprland/src/layout/algorithm/Algorithm.hpp>
#include <hyprland/src/layout/space/Space.hpp>
#include <hyprland/src/helpers/Monitor.hpp>
#include <optional>

#include "dispatchers.h"
#include "scroller.h"

extern HANDLE PHANDLE;

namespace {
    // Resolve scroller layout instance from workspace id, returning nullptr if that
    // workspace is not currently managed by this plugin.
    ScrollerLayout *getScrollerForWorkspace(const int workspace_id) {
        const auto workspace = g_pCompositor->getWorkspaceByID(workspace_id);
        if (!workspace || !workspace->m_space) {
            return nullptr;
        }

        const auto algorithm = workspace->m_space->algorithm();
        if (!algorithm) {
            return nullptr;
        }

        const auto &tiled = algorithm->tiledAlgo();
        if (!tiled) {
            return nullptr;
        }

        return dynamic_cast<ScrollerLayout *>(tiled.get());
    }

    // Resolve layout by cursor position and current active workspace, while
    // excluding fullscreen contexts that should not react to plugin actions.
    ScrollerLayout *layout_for_action(int *workspace) {
        PHLMONITOR monitor = g_pCompositor->getMonitorFromCursor();
        if (!monitor) {
            if (workspace)
                *workspace = -1;
            return nullptr;
        }

        int workspace_id = monitor->activeSpecialWorkspaceID();
        if (workspace_id == WORKSPACE_INVALID) {
            workspace_id = monitor->activeWorkspaceID();
        }

        const auto PWORKSPACE = g_pCompositor->getWorkspaceByID(workspace_id);
        if (workspace_id == WORKSPACE_INVALID || !PWORKSPACE || PWORKSPACE->m_hasFullscreenWindow) {
            if (workspace)
                *workspace = -1;
            return nullptr;
        }

        if (workspace)
            *workspace = workspace_id;

        return getScrollerForWorkspace(workspace_id);
    }

    // Parse direction-like arguments used by movefocus/movewindow/alignwindow.
    std::optional<Direction> parse_move_arg(std::string arg) {
        if (arg == "l" || arg == "left")
            return Direction::Left;
        if (arg == "r" || arg == "right")
            return Direction::Right;
        if (arg == "u" || arg == "up")
            return Direction::Up;
        if (arg == "d" || arg == "dn" || arg == "down")
            return Direction::Down;
        if (arg == "b" || arg == "begin" || arg == "beginning")
            return Direction::Begin;
        if (arg == "e" || arg == "end")
            return Direction::End;
        if (arg == "c" || arg == "center" || arg == "centre")
            return Direction::Center;
        return {};
    }

    // Parse fit mode arguments for the fitsize dispatcher.
    std::optional<FitSize> parse_fit_size(std::string arg) {
        if (arg == "active")
            return FitSize::Active;
        if (arg == "visible")
            return FitSize::Visible;
        if (arg == "all")
            return FitSize::All;
        if (arg == "toend")
            return FitSize::ToEnd;
        if (arg == "tobeg" || arg == "tobeginning")
            return FitSize::ToBeg;
        return {};
    }

    // cyclesize(+1|-1): change active row/column width/height step.
    void dispatch_cyclesize(std::string arg) {
        int workspace;
        auto layout = layout_for_action(&workspace);
        if (!layout || workspace == -1)
            return;

        int step = 0;
        if (arg == "+1" || arg == "1" || arg == "next") {
            step = 1;
        } else if (arg == "-1" || arg == "prev" || arg == "previous") {
            step = -1;
        } else {
            return;
        }
        layout->cycle_window_size(workspace, step);
    }

    // movefocus <dir>: move focus inside scroller layout, with optional monitor
    // fallback when the active row cannot move in requested direction.
    void dispatch_movefocus(std::string arg) {
        int workspace;
        auto layout = layout_for_action(&workspace);
        if (!layout || workspace == -1)
            return;

        auto args = CVarList(arg);
        if (auto direction = parse_move_arg(args[0])) {
            layout->move_focus(workspace, *direction);
        }
    }

    // movewindow <dir>: reorder active window inside row/column.
    void dispatch_movewindow(std::string arg) {
        int workspace;
        auto layout = layout_for_action(&workspace);
        if (!layout || workspace == -1)
            return;

        auto args = CVarList(arg);
        if (auto direction = parse_move_arg(args[0])) {
            layout->move_window(workspace, *direction);
        }
    }

    // alignwindow <dir>: align active window/column against row/column geometry.
    void dispatch_alignwindow(std::string arg) {
        int workspace;
        auto layout = layout_for_action(&workspace);
        if (!layout || workspace == -1)
            return;

        auto args = CVarList(arg);
        if (auto direction = parse_move_arg(args[0])) {
            layout->align_window(workspace, *direction);
        }
    }

    // admitwindow: split active column and move focused window to previous column.
    void dispatch_admitwindow(std::string arg) {
        int workspace;
        auto layout = layout_for_action(&workspace);
        if (!layout || workspace == -1)
            return;
        (void)arg;
        layout->admit_window_left(workspace);
    }

    // expelwindow: remove focused window from current column into a new one right
    // after it.
    void dispatch_expelwindow(std::string arg) {
        int workspace;
        auto layout = layout_for_action(&workspace);
        if (!layout || workspace == -1)
            return;
        (void)arg;
        layout->expel_window_right(workspace);
    }

    // resetheight: currently reserved for future behavior, kept as a command stub.
    void dispatch_resetheight(std::string arg) {
        int workspace;
        auto layout = layout_for_action(&workspace);
        if (!layout || workspace == -1)
            return;
        (void)arg;
        layout->reset_height(workspace);
    }

    // setmode row|col: switch between row mode and column mode.
    void dispatch_setmode(std::string arg) {
        int workspace;
        auto layout = layout_for_action(&workspace);
        if (!layout || workspace == -1)
            return;

        Mode mode = Mode::Row;
        if (arg == "r" || arg == "row") {
            mode = Mode::Row;
        } else if (arg == "c" || arg == "col" || arg == "column") {
            mode = Mode::Column;
        }
        layout->set_mode(workspace, mode);
    }

    // fitsize <active|visible|all|toend|tobeg>: resize visible windows so they
    // fit requested range.
    void dispatch_fitsize(std::string arg) {
        int workspace;
        auto layout = layout_for_action(&workspace);
        if (!layout || workspace == -1)
            return;

        auto args = CVarList(arg);
        if (auto fitsize = parse_fit_size(args[0])) {
            layout->fit_size(workspace, *fitsize);
        }
    }

    // toggleoverview: switch overview miniaturized layout mode for active workspace.
    void dispatch_toggleoverview(std::string arg) {
        int workspace;
        auto layout = layout_for_action(&workspace);
        if (!layout || workspace == -1)
            return;
        (void)arg;
        layout->toggle_overview(workspace);
    }

    // marksadd <name>: save focused window under a named mark.
    void dispatch_marksadd(std::string arg) {
        int workspace;
        auto layout = layout_for_action(&workspace);
        if (!layout || workspace == -1)
            return;
        layout->marks_add(arg);
    }

    // marksdelete <name>: remove a stored mark entry by name.
    void dispatch_marksdelete(std::string arg) {
        auto layout = layout_for_action(new int);
        if (!layout)
            return;
        (void)arg;
        layout->marks_delete(arg);
    }

    // marksvisit <name>: focus and activate the marked window if present.
    void dispatch_marksvisit(std::string arg) {
        auto layout = layout_for_action(new int);
        if (!layout)
            return;
        layout->marks_visit(arg);
    }

    // marksreset: clear all marks.
    void dispatch_marksreset(std::string arg) {
        auto layout = layout_for_action(new int);
        if (!layout)
            return;
        (void)arg;
        layout->marks_reset();
    }
}

// Register all plugin dispatchers into Hyprland's dispatcher map.
void dispatchers::addDispatchers() {
    const auto wrap = [](auto fn) {
        return [fn = std::move(fn)](const std::string& arg) -> SDispatchResult {
            fn(arg);
            return {};
        };
    };

    HyprlandAPI::addDispatcherV2(PHANDLE, "scroller:cyclesize", wrap(dispatch_cyclesize));
    HyprlandAPI::addDispatcherV2(PHANDLE, "scroller:movefocus", wrap(dispatch_movefocus));
    HyprlandAPI::addDispatcherV2(PHANDLE, "scroller:movewindow", wrap(dispatch_movewindow));
    HyprlandAPI::addDispatcherV2(PHANDLE, "scroller:alignwindow", wrap(dispatch_alignwindow));
    HyprlandAPI::addDispatcherV2(PHANDLE, "scroller:admitwindow", wrap(dispatch_admitwindow));
    HyprlandAPI::addDispatcherV2(PHANDLE, "scroller:expelwindow", wrap(dispatch_expelwindow));
    HyprlandAPI::addDispatcherV2(PHANDLE, "scroller:setmode", wrap(dispatch_setmode));
    HyprlandAPI::addDispatcherV2(PHANDLE, "scroller:fitsize", wrap(dispatch_fitsize));
    HyprlandAPI::addDispatcherV2(PHANDLE, "scroller:toggleoverview", wrap(dispatch_toggleoverview));
    HyprlandAPI::addDispatcherV2(PHANDLE, "scroller:marksadd", wrap(dispatch_marksadd));
    HyprlandAPI::addDispatcherV2(PHANDLE, "scroller:marksdelete", wrap(dispatch_marksdelete));
    HyprlandAPI::addDispatcherV2(PHANDLE, "scroller:marksvisit", wrap(dispatch_marksvisit));
    HyprlandAPI::addDispatcherV2(PHANDLE, "scroller:marksreset", wrap(dispatch_marksreset));
}
