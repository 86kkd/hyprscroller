/**
 * @file overview/render/render.h
 * @brief Render-pass overview overlay bootstrap.
 */
#pragma once

#include <hyprland/src/helpers/Monitor.hpp>
#include <hyprland/src/plugins/HookSystem.hpp>

namespace Overview {

bool initializeRendererHooks(HANDLE handle);
void shutdownRendererHooks(HANDLE handle);
void fullRenderMonitor(PHLMONITOR monitor);

} // namespace Overview
