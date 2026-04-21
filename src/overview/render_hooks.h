/**
 * @file render_hooks.h
 * @brief Internal overview render hook bootstrap.
 */
#pragma once

#include <hyprland/src/plugins/HookSystem.hpp>

namespace Overview {

bool initializeRendererHooksImpl(HANDLE handle);
void shutdownRendererHooksImpl(HANDLE handle);

} // namespace Overview
