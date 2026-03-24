/**
 * @file dispatch.cpp
 * @brief Shared Hyprland dispatcher invocation helpers for canvas code.
 *
 * This file centralizes lookup, validation, logging and invocation of Hyprland
 * dispatchers so higher-level focus/window routing code no longer touches the
 * raw dispatcher map directly.
 */
#include <cstdio>
#include <string>
#include <type_traits>

#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/helpers/Monitor.hpp>
#include <hyprland/src/managers/KeybindManager.hpp>
#include <spdlog/spdlog.h>

#include "../../core/direction.h"
#include "internal.h"

namespace {
using DispatcherHandler = std::decay_t<decltype(g_pKeybindManager->m_dispatchers.begin()->second)>;

const char *dispatcher_context(const char *context, const char *dispatcher) {
    if (context && context[0] != '\0')
        return context;
    if (dispatcher && dispatcher[0] != '\0')
        return dispatcher;
    return "dispatcher";
}

DispatcherHandler *resolve_dispatcher(const char *dispatcher, std::string_view arg, const char *context) {
    const auto *ctx = dispatcher_context(context, dispatcher);
    if (!dispatcher || dispatcher[0] == '\0') {
        spdlog::warn("{}: missing dispatcher name", ctx);
        return nullptr;
    }

    if (arg.empty()) {
        spdlog::warn("{}: empty dispatcher arg dispatcher={}", ctx, dispatcher);
        return nullptr;
    }

    if (!g_pKeybindManager) {
        spdlog::warn("{}: keybind manager unavailable dispatcher={}", ctx, dispatcher);
        return nullptr;
    }

    const auto it = g_pKeybindManager->m_dispatchers.find(dispatcher);
    if (it == g_pKeybindManager->m_dispatchers.end()) {
        spdlog::warn("{}: dispatcher not found dispatcher={}", ctx, dispatcher);
        return nullptr;
    }

    return &it->second;
}
} // namespace

namespace CanvasLayoutInternal {

bool can_invoke_dispatcher(const char* dispatcher, std::string_view arg, const char* context) {
    return resolve_dispatcher(dispatcher, arg, context) != nullptr;
}

bool invoke_dispatcher(const char* dispatcher, std::string_view arg, const char* context) {
    auto *handler = resolve_dispatcher(dispatcher, arg, context);
    if (!handler)
        return false;

    (*handler)(std::string(arg));
    return true;
}

// Shared wrapper around Hyprland builtin directional dispatchers.
void dispatch_directional_builtin(const char* dispatcher, Direction direction) {
    const auto arg = ScrollerCore::direction_dispatch_arg(direction);
    if (!arg) {
        spdlog::warn("dispatch_directional_builtin: unsupported direction={} dispatcher={}",
                     ScrollerCore::direction_name(direction),
                     dispatcher ? dispatcher : "(null)");
        return;
    }

    (void)invoke_dispatcher(dispatcher, arg, "dispatch_directional_builtin");
}

// Thin specialized wrapper for builtin movefocus.
void dispatch_builtin_movefocus(Direction direction) {
    dispatch_directional_builtin("movefocus", direction);
}

// Focus the monitor hosting a target window before focusing the window itself.
void focus_window_monitor(PHLWINDOW window) {
    if (!window)
        return;

    const auto targetMonitor = g_pCompositor->getMonitorFromID(window->monitorID());
    const auto currentMonitor = g_pCompositor->getMonitorFromCursor();
    if (!targetMonitor || !currentMonitor || targetMonitor == currentMonitor || targetMonitor->m_name.empty())
        return;

    spdlog::debug("switch_to_window: focusing monitor={} before window={} workspace={}",
                  targetMonitor->m_name,
                  static_cast<const void*>(window.get()),
                  window->workspaceID());
    (void)invoke_dispatcher("focusmonitor", targetMonitor->m_name, "focus_window_monitor");
}

// Focus a target window and optionally warp the cursor to it.
void switch_to_window(PHLWINDOW window, bool warp_cursor)
{
    if (!window)
        return;

    focus_window_monitor(window);

    if (!g_pCompositor->isWindowActive(window)) {
        spdlog::debug("switch_to_window: focusing window={} workspace={}",
                      static_cast<const void*>(window.get()), window->workspaceID());
        char selector[64];
        std::snprintf(selector, sizeof(selector), "address:0x%lx",
                      reinterpret_cast<unsigned long>(window.get()));
        (void)invoke_dispatcher("focuswindow", selector, "switch_to_window");
    }

    if (warp_cursor)
        window->warpCursor(true);
}

} // namespace CanvasLayoutInternal
