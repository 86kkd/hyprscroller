/**
 * @file dispatchers.cpp
 * @brief Hyprland dispatcher registration and argument parsing glue.
 *
 * The functions in this file translate Hyprland string dispatcher arguments
 * into strongly typed plugin commands, resolve the active canvas layout for the
 * current monitor/workspace context, and then forward execution into
 * `CanvasLayout`.
 */
#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/includes.hpp>
#include <hyprlang.hpp>
#include <hyprland/src/plugins/PluginAPI.hpp>
#include <hyprutils/string/VarList.hpp>
#include <hyprland/src/layout/algorithm/Algorithm.hpp>
#include <hyprland/src/layout/space/Space.hpp>
#include <hyprland/src/helpers/Monitor.hpp>
#include <spdlog/spdlog.h>
#include <optional>
#include <string_view>

#include "core/direction.h"
#include "dispatchers.h"
#include "layout/canvas/layout.h"
#include "overview/session.h"

extern HANDLE PHANDLE;

namespace {
    bool is_overview_cancel_arg(std::string_view arg) {
        return arg == "cancel" || arg == "abort" || arg == "close";
    }

    bool is_overview_accept_arg(std::string_view arg) {
        return arg == "accept" || arg == "confirm" || arg == "enter";
    }

    // Resolve canvas layout instance from workspace id, returning nullptr when
    // the workspace is not currently managed by this plugin.
    CanvasLayout *getCanvasForWorkspace(const int workspace_id) {
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

        return dynamic_cast<CanvasLayout *>(tiled.get());
    }

    // Pick the visible workspace on a monitor, preferring special workspaces.
    PHLWORKSPACE getWorkspaceForAction(PHLMONITOR monitor) {
        if (!monitor)
            return nullptr;

        const auto special_workspace_id = monitor->activeSpecialWorkspaceID();
        if (const auto special_workspace = g_pCompositor->getWorkspaceByID(special_workspace_id))
            return special_workspace;

        const auto active_workspace_id = monitor->activeWorkspaceID();
        if (active_workspace_id == WORKSPACE_INVALID)
            return nullptr;

        return g_pCompositor->getWorkspaceByID(active_workspace_id);
    }

    // Resolve layout by cursor position and current active workspace, while
    // excluding fullscreen contexts that should not react to plugin actions.
    CanvasLayout *layout_for_action(int *workspace) {
        if (Overview::session().active()) {
            spdlog::debug("layout_for_action: ignored while global overview is active");
            if (workspace)
                *workspace = -1;
            return nullptr;
        }

        PHLMONITOR monitor = g_pCompositor->getMonitorFromCursor();
        if (!monitor) {
            spdlog::warn("layout_for_action: no monitor under cursor");
            if (workspace)
                *workspace = -1;
            return nullptr;
        }

        const auto special_workspace_id = monitor->activeSpecialWorkspaceID();
        const auto active_workspace_id = monitor->activeWorkspaceID();
        const auto special_workspace = g_pCompositor->getWorkspaceByID(special_workspace_id);
        const auto selected_workspace = getWorkspaceForAction(monitor);
        const auto workspace_id = selected_workspace ? selected_workspace->m_id : WORKSPACE_INVALID;

        if (!selected_workspace || selected_workspace->m_hasFullscreenWindow) {
            spdlog::debug("layout_for_action: rejected chosen_ws={} special_ws={} active_ws={} special_exists={} exists={} fullscreen={}",
                          workspace_id, special_workspace_id, active_workspace_id, special_workspace != nullptr, selected_workspace != nullptr,
                          selected_workspace ? selected_workspace->m_hasFullscreenWindow : false);
            if (workspace)
                *workspace = -1;
            return nullptr;
        }

        spdlog::debug("layout_for_action: selected chosen_ws={} special_ws={} active_ws={} special_exists={}",
                      workspace_id, special_workspace_id, active_workspace_id, special_workspace != nullptr);
        if (workspace)
            *workspace = workspace_id;

        auto *layout = getCanvasForWorkspace(workspace_id);
        if (layout)
            layout->prepareForActionContext();
        return layout;
    }

    // cyclesize(+1|-1): change active stack width/height step.
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
    // fallback when the active lane cannot move in requested direction.
    void dispatch_movefocus(std::string arg) {
        auto args = CVarList(arg);
        const auto direction = ScrollerCore::parse_direction_arg(args[0]);
        if (!direction) {
            spdlog::warn("dispatch_movefocus: unsupported arg='{}'", arg);
            return;
        }

        if (Overview::session().active()) {
            spdlog::info("dispatch_movefocus: overview arg='{}'", arg);
            (void)Overview::session().moveSelection(*direction);
            return;
        }

        int workspace;
        auto layout = layout_for_action(&workspace);
        if (!layout || workspace == -1) {
            spdlog::warn("dispatch_movefocus: no layout for arg='{}'", arg);
            return;
        }

        spdlog::info("dispatch_movefocus: arg='{}' workspace={}", arg, workspace);
        layout->move_focus(workspace, *direction);
    }

    // movewindow <dir>: reorder active window inside lane/stack.
    void dispatch_movewindow(std::string arg) {
        int workspace;
        auto layout = layout_for_action(&workspace);
        if (!layout || workspace == -1)
            return;

        auto args = CVarList(arg);
        if (auto direction = ScrollerCore::parse_direction_arg(args[0])) {
            layout->move_window(workspace, *direction);
        }
    }

    // alignwindow <dir>: align active window/stack against lane/stack geometry.
    void dispatch_alignwindow(std::string arg) {
        int workspace;
        auto layout = layout_for_action(&workspace);
        if (!layout || workspace == -1)
            return;

        auto args = CVarList(arg);
        if (auto direction = ScrollerCore::parse_direction_arg(args[0])) {
            layout->align_window(workspace, *direction);
        }
    }

    // admitwindow: split active stack and move focused window to the previous stack.
    void dispatch_admitwindow(std::string arg) {
        int workspace;
        auto layout = layout_for_action(&workspace);
        if (!layout || workspace == -1)
            return;
        (void)arg;
        layout->admit_window_left(workspace);
    }

    // expelwindow: remove focused window from current stack into a new one right
    // after it.
    void dispatch_expelwindow(std::string arg) {
        int workspace;
        auto layout = layout_for_action(&workspace);
        if (!layout || workspace == -1)
            return;
        (void)arg;
        layout->expel_window_right(workspace);
    }

    // setmode row|col: switch between row mode and column mode.
    void dispatch_setmode(std::string arg) {
        int workspace;
        auto layout = layout_for_action(&workspace);
        if (!layout || workspace == -1)
            return;

        auto args = CVarList(arg);
        const auto mode = ScrollerCore::parse_mode_arg(args[0]);
        if (!mode) {
            spdlog::warn("dispatch_setmode: unsupported arg='{}'", arg);
            return;
        }

        layout->set_mode(workspace, *mode);
    }

    // fitsize <active|visible|all|toend|tobeg>: resize visible windows so they
    // fit requested range.
    void dispatch_fitsize(std::string arg) {
        int workspace;
        auto layout = layout_for_action(&workspace);
        if (!layout || workspace == -1)
            return;

        auto args = CVarList(arg);
        if (auto fitsize = ScrollerCore::parse_fit_size_arg(args[0])) {
            layout->fit_size(workspace, *fitsize);
        }
    }

    // toggleoverview: enter or accept the global logical overview session.
    void dispatch_toggleoverview(std::string arg) {
        auto& overview = Overview::session();
        if (is_overview_cancel_arg(arg)) {
            if (overview.active())
                overview.close(false);
            return;
        }

        if (is_overview_accept_arg(arg)) {
            if (overview.active())
                overview.close(true);
            else
                overview.open();
            return;
        }

        if (overview.active()) {
            overview.close(true);
            return;
        }

        overview.open();
    }

    // canceloverview: leave the global overview session without accepting.
    void dispatch_canceloverview(std::string arg) {
        (void)arg;
        auto& overview = Overview::session();
        if (overview.active())
            overview.close(false);
    }

    // togglelaneoverview: switch the old lane-local geometry overview mode.
    void dispatch_togglelaneoverview(std::string arg) {
        int workspace;
        auto layout = layout_for_action(&workspace);
        if (!layout || workspace == -1)
            return;
        (void)arg;
        layout->toggle_overview(workspace);
    }

    // togglefullscreen: expand the active scroller window to the monitor bounds.
    void dispatch_togglefullscreen(std::string arg) {
        int workspace;
        auto layout = layout_for_action(&workspace);
        if (!layout || workspace == -1)
            return;
        (void)arg;
        layout->toggle_fullscreen(workspace);
    }

    // createlane <dir>: move the active stack into a new adjacent lane.
    void dispatch_createlane(std::string arg) {
        int workspace;
        auto layout = layout_for_action(&workspace);
        if (!layout || workspace == -1)
            return;

        auto args = CVarList(arg);
        if (auto direction = ScrollerCore::parse_direction_arg(args[0])) {
            layout->create_lane(workspace, *direction);
        }
    }

    // focuslane <dir>: switch the active lane inside the current canvas.
    void dispatch_focuslane(std::string arg) {
        int workspace;
        auto layout = layout_for_action(&workspace);
        if (!layout || workspace == -1)
            return;

        auto args = CVarList(arg);
        if (auto direction = ScrollerCore::parse_direction_arg(args[0])) {
            layout->focus_lane(workspace, *direction);
        }
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
        auto layout = layout_for_action(nullptr);
        if (!layout)
            return;
        layout->marks_delete(arg);
    }

    // marksvisit <name>: focus and activate the marked window if present.
    void dispatch_marksvisit(std::string arg) {
        auto layout = layout_for_action(nullptr);
        if (!layout)
            return;
        layout->marks_visit(arg);
    }

    // marksreset: clear all marks.
    void dispatch_marksreset(std::string arg) {
        auto layout = layout_for_action(nullptr);
        if (!layout)
            return;
        (void)arg;
        layout->marks_reset();
    }
}

// Register all plugin dispatchers into Hyprland's dispatcher map.
void dispatchers::addDispatchers() {
    // Wrap raw std::string handlers into Hyprland's dispatcher result type.
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
    HyprlandAPI::addDispatcherV2(PHANDLE, "scroller:canceloverview", wrap(dispatch_canceloverview));
    HyprlandAPI::addDispatcherV2(PHANDLE, "scroller:togglelaneoverview", wrap(dispatch_togglelaneoverview));
    HyprlandAPI::addDispatcherV2(PHANDLE, "scroller:togglefullscreen", wrap(dispatch_togglefullscreen));
    HyprlandAPI::addDispatcherV2(PHANDLE, "scroller:createlane", wrap(dispatch_createlane));
    HyprlandAPI::addDispatcherV2(PHANDLE, "scroller:focuslane", wrap(dispatch_focuslane));
    HyprlandAPI::addDispatcherV2(PHANDLE, "scroller:marksadd", wrap(dispatch_marksadd));
    HyprlandAPI::addDispatcherV2(PHANDLE, "scroller:marksdelete", wrap(dispatch_marksdelete));
    HyprlandAPI::addDispatcherV2(PHANDLE, "scroller:marksvisit", wrap(dispatch_marksvisit));
    HyprlandAPI::addDispatcherV2(PHANDLE, "scroller:marksreset", wrap(dispatch_marksreset));
}
