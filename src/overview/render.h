/**
 * @file render.h
 * @brief Hook-based overview preview renderer bootstrap.
 */
#pragma once

#include <hyprland/src/plugins/HookSystem.hpp>

namespace Overview {

bool initializeRendererHooks(HANDLE handle);
void shutdownRendererHooks(HANDLE handle);

} // namespace Overview
