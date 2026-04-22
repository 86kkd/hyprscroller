/**
 * @file dispatchers_internal.h
 * @brief Shared internal helpers for dispatcher registration and argument parsing.
 */
#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/helpers/Monitor.hpp>
#include <hyprland/src/includes.hpp>
#include <hyprland/src/layout/algorithm/Algorithm.hpp>
#include <hyprland/src/layout/space/Space.hpp>
#include <hyprland/src/plugins/PluginAPI.hpp>
#include <hyprlang.hpp>
#include <hyprutils/string/VarList.hpp>
#include <spdlog/spdlog.h>

#include "core/direction.h"
#include "layout/canvas/internal.h"
#include "layout/canvas/layout.h"
#include "overview/session.h"

extern HANDLE PHANDLE;

namespace dispatchers::detail {

struct WorkspaceActionLayout {
    CanvasLayout* layout = nullptr;
    int           workspace = -1;

    explicit operator bool() const {
        return layout != nullptr && workspace != -1;
    }
};

inline bool isOverviewCancelArg(std::string_view arg) {
    return arg == "cancel" || arg == "abort" || arg == "close";
}

inline bool isOverviewAcceptArg(std::string_view arg) {
    return arg == "accept" || arg == "confirm" || arg == "enter";
}

inline CanvasLayout* getCanvasForWorkspace(const int workspaceId) {
    const auto workspace = g_pCompositor->getWorkspaceByID(workspaceId);
    if (!workspace || !workspace->m_space)
        return nullptr;

    const auto algorithm = workspace->m_space->algorithm();
    if (!algorithm)
        return nullptr;

    const auto& tiled = algorithm->tiledAlgo();
    if (!tiled)
        return nullptr;

    return dynamic_cast<CanvasLayout*>(tiled.get());
}

inline PHLWORKSPACE getWorkspaceForAction(PHLMONITOR monitor) {
    if (!monitor)
        return nullptr;

    const auto specialWorkspaceId = monitor->activeSpecialWorkspaceID();
    if (const auto specialWorkspace = g_pCompositor->getWorkspaceByID(specialWorkspaceId))
        return specialWorkspace;

    const auto activeWorkspaceId = monitor->activeWorkspaceID();
    if (activeWorkspaceId == WORKSPACE_INVALID)
        return nullptr;

    return g_pCompositor->getWorkspaceByID(activeWorkspaceId);
}

inline CanvasLayout* layoutForAction(int* workspace) {
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

    const auto specialWorkspaceId = monitor->activeSpecialWorkspaceID();
    const auto activeWorkspaceId = monitor->activeWorkspaceID();
    const auto specialWorkspace = g_pCompositor->getWorkspaceByID(specialWorkspaceId);
    const auto selectedWorkspace = getWorkspaceForAction(monitor);
    const auto workspaceId = selectedWorkspace ? selectedWorkspace->m_id : WORKSPACE_INVALID;

    if (!selectedWorkspace || selectedWorkspace->m_hasFullscreenWindow) {
        spdlog::debug("layout_for_action: rejected chosen_ws={} special_ws={} active_ws={} special_exists={} exists={} fullscreen={}",
                      workspaceId,
                      specialWorkspaceId,
                      activeWorkspaceId,
                      specialWorkspace != nullptr,
                      selectedWorkspace != nullptr,
                      selectedWorkspace ? selectedWorkspace->m_hasFullscreenWindow : false);
        if (workspace)
            *workspace = -1;
        return nullptr;
    }

    spdlog::debug("layout_for_action: selected chosen_ws={} special_ws={} active_ws={} special_exists={}",
                  workspaceId,
                  specialWorkspaceId,
                  activeWorkspaceId,
                  specialWorkspace != nullptr);
    if (workspace)
        *workspace = workspaceId;

    auto* layout = getCanvasForWorkspace(workspaceId);
    if (layout)
        layout->prepareForActionContext();
    return layout;
}

inline WorkspaceActionLayout workspaceLayoutForAction() {
    WorkspaceActionLayout action;
    action.layout = layoutForAction(&action.workspace);
    return action;
}

template <typename Fn>
inline void withWorkspaceLayout(Fn&& fn) {
    const auto action = workspaceLayoutForAction();
    if (!action)
        return;

    fn(*action.layout, action.workspace);
}

template <typename Fn>
inline void withLayout(Fn&& fn) {
    auto* layout = layoutForAction(nullptr);
    if (!layout)
        return;

    fn(*layout);
}

inline std::string firstDispatchArg(const std::string& arg) {
    auto args = CVarList(arg);
    return args[0];
}

inline std::optional<Direction> parsedDirectionArg(const std::string& arg) {
    return ScrollerCore::parse_direction_arg(firstDispatchArg(arg));
}

inline std::optional<Mode> parsedModeArg(const std::string& arg) {
    return ScrollerCore::parse_mode_arg(firstDispatchArg(arg));
}

inline std::optional<FitSize> parsedFitSizeArg(const std::string& arg) {
    return ScrollerCore::parse_fit_size_arg(firstDispatchArg(arg));
}

template <typename Fn>
inline void withParsedDirectionArg(const std::string& arg, Fn&& fn) {
    if (const auto direction = parsedDirectionArg(arg))
        fn(*direction);
}

template <typename Fn>
inline void withParsedFitSizeArg(const std::string& arg, Fn&& fn) {
    if (const auto fitSize = parsedFitSizeArg(arg))
        fn(*fitSize);
}

template <typename Fn>
inline void withParsedModeArgOrWarn(const char* dispatcher, const std::string& arg, Fn&& fn) {
    const auto mode = parsedModeArg(arg);
    if (!mode) {
        spdlog::warn("{}: unsupported arg='{}'", dispatcher, arg);
        return;
    }

    fn(*mode);
}

template <typename Fn>
inline void withWorkspaceDirectionArg(const std::string& arg, Fn&& fn) {
    withWorkspaceLayout([&](CanvasLayout& layout, int workspace) {
        withParsedDirectionArg(arg, [&](Direction direction) {
            fn(layout, workspace, direction);
        });
    });
}

inline Overview::Session& overviewSessionForDispatch() {
    auto& overview = Overview::session();
    overview.markInputHandled();
    return overview;
}

inline void closeOverviewIfActive(bool acceptSelection) {
    auto& overview = overviewSessionForDispatch();
    if (overview.active())
        overview.close(acceptSelection);
}

template <typename Fn>
inline void registerDispatcher(const char* name, Fn&& fn) {
    HyprlandAPI::addDispatcherV2(PHANDLE, name, [fn = std::forward<Fn>(fn)](const std::string& arg) -> SDispatchResult {
        fn(arg);
        return {};
    });
}

void registerLayoutDispatchers();
void registerOverviewMarkDispatchers();

} // namespace dispatchers::detail
