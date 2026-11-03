/**
 * @file plugin/dispatch/registration.h
 * @brief Dispatcher registration entrypoint for scroller layout commands.
 *
 * Hyprland 0.57 exposes plugin commands through Lua functions. This header
 * keeps the registration API intentionally narrow so the rest of the plugin
 * can treat command wiring as a single bootstrap step.
 */
#pragma once

namespace dispatchers {
    // Register functions exposed as `hl.plugin.scroller.*` to Lua configs.
    void addLuaFunctions();
}
