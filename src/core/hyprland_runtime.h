#pragma once

#include <hyprland/src/desktop/state/FocusState.hpp>
#include <hyprland/src/desktop/state/WindowState.hpp>
#include <hyprland/src/desktop/view/Window.hpp>
#include <hyprland/src/managers/fullscreen/FullscreenController.hpp>
#include <hyprland/src/pointer/PointerManager.hpp>
#include <hyprland/src/state/MonitorState.hpp>
#include <hyprland/src/state/WorkspaceState.hpp>

namespace ScrollerCore::HyprlandRuntime {

inline PHLWORKSPACE workspaceById(WORKSPACEID id) {
    return State::workspaceState()->query().id(id).run();
}

inline PHLMONITOR monitorById(MONITORID id) {
    return State::monitorState()->query().id(id).run();
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

inline Vector2D windowPosition(PHLWINDOW window) {
    return window ? window->position(Desktop::View::IGeometric::GEOMETRIC_GOAL) : Vector2D{};
}

inline Vector2D windowSize(PHLWINDOW window) {
    return window ? window->size(Desktop::View::IGeometric::GEOMETRIC_GOAL) : Vector2D{};
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
