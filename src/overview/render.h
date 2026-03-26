/**
 * @file render.h
 * @brief Hook-based overview preview renderer bootstrap.
 */
#pragma once

#include <hyprland/src/helpers/Monitor.hpp>
#include <hyprland/src/plugins/HookSystem.hpp>

namespace Overview {

bool initializeRendererHooks(HANDLE handle);
void shutdownRendererHooks(HANDLE handle);
void fullRenderMonitor(PHLMONITOR monitor);

} // namespace Overview
