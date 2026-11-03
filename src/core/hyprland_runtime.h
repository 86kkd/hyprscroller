#pragma once

#include <hyprland/src/desktop/state/FocusState.hpp>
#include <hyprland/src/desktop/state/WindowState.hpp>
#include <hyprland/src/desktop/view/window/Window.hpp>
#include <hyprland/src/desktop/view/window/WindowPresentation.hpp>
#include <hyprland/src/managers/fullscreen/FullscreenController.hpp>
#include <hyprland/src/pointer/PointerManager.hpp>
#include <hyprland/src/state/MonitorState.hpp>
#include <hyprland/src/state/WorkspaceState.hpp>

#include "types.h"

namespace ScrollerCore::HyprlandRuntime {

inline WORKSPACEID workspaceId(PHLWORKSPACE workspace) {
    if (!workspace)
        return WORKSPACE_INVALID;

    const auto id = workspace->id();
    if (const auto* numbered = std::get_if<Workspace::SWorkspaceNumberedID>(&id))
        return static_cast<WORKSPACEID>(numbered->value);

    uint64_t hash = 1469598103934665603ULL;
    for (const unsigned char byte : workspace->addressableName()) {
        hash ^= byte;
        hash *= 1099511628211ULL;
    }
    return -2 - static_cast<WORKSPACEID>(hash & 0x3FFFFFFFULL);
}

inline PHLWORKSPACE workspaceById(WORKSPACEID id) {
    if (id >= 0)
        return State::workspaceState()->query().numbered(Workspace::SWorkspaceNumberedID(static_cast<Workspace::WorkspaceIDContainer>(id))).run();

    for (const auto& workspaceRef : State::workspaceState()->workspaceRefs()) {
        const auto workspace = workspaceRef.lock();
        if (workspaceId(workspace) == id)
            return workspace;
    }
    return nullptr;
}

inline PHLMONITOR monitorById(MONITORID id) {
    return State::monitorState()->query().id(id).run();
}

inline WORKSPACEID activeWorkspaceId(PHLMONITOR monitor) {
    return monitor ? workspaceId(monitor->m_activeWorkspace) : WORKSPACE_INVALID;
}

inline WORKSPACEID activeSpecialWorkspaceId(PHLMONITOR monitor) {
    return monitor ? workspaceId(monitor->m_activeSpecialWorkspace) : WORKSPACE_INVALID;
}

inline PHLMONITOR monitorFromCursor() {
    return State::monitorState()->query().vec(Pointer::mgr()->position()).run();
}

inline PHLMONITOR monitorInDirection(PHLMONITOR source, Math::eDirection direction) {
    if (!source)
        return nullptr;
    return State::monitorState()->query().inDirection(direction).relativeTo(source).run();
}

inline PHLMONITOR monitorInDirection(Math::eDirection direction) {
    auto source = Desktop::focusState()->monitor();
    if (!source)
        source = monitorFromCursor();
    return monitorInDirection(source, direction);
}

inline const std::vector<PHLMONITOR>& monitors() {
    return State::monitorState()->monitors();
}

inline const std::vector<PHLWORKSPACEREF>& workspaces() {
    return State::workspaceState()->workspaceRefs();
}

inline const std::vector<PHLWINDOW>& windows() {
    return Desktop::windowState()->windows();
}

inline bool isWindowActive(PHLWINDOW window) {
    return Desktop::focusState()->isWindowActive(window);
}

inline WORKSPACEID windowWorkspaceId(PHLWINDOW window) {
    return window ? workspaceId(window->m_workspace) : WORKSPACE_INVALID;
}

inline bool isSpecialWorkspace(PHLWORKSPACE workspace) {
    return workspace && workspace->type() == Workspace::eWorkspaceType::SPECIAL;
}

inline Vector2D windowPosition(PHLWINDOW window) {
    return window ? window->position(Desktop::View::IGeometric::GEOMETRIC_GOAL) : Vector2D{};
}

inline Vector2D windowSize(PHLWINDOW window) {
    return window ? window->size(Desktop::View::IGeometric::GEOMETRIC_GOAL) : Vector2D{};
}

inline double windowBorderSize(PHLWINDOW window) {
    return window ? window->presentation().borderSize() : 0.0;
}

inline void setWindowGeometry(PHLWINDOW window, const Vector2D& position, const Vector2D& size) {
    if (window)
        window->setBox(CBox{position, size});
}

inline bool workspaceHasFullscreen(PHLWORKSPACE workspace) {
    return workspace && Fullscreen::controller()->hasFullscreen(workspace);
}

inline bool windowIsFullscreen(PHLWINDOW window) {
    return window && Fullscreen::controller()->isFullscreen(window);
}

inline Fullscreen::eFullscreenMode workspaceFullscreenMode(PHLWORKSPACE workspace) {
    if (!workspace)
        return Fullscreen::FSMODE_NONE;
    return Fullscreen::controller()->getFullscreenModes(workspace).internal;
}

} // namespace ScrollerCore::HyprlandRuntime
