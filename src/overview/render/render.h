/**
 * @file overview/render/render.h
 * @brief Render-pass overview overlay bootstrap.
 */
#pragma once

#include <hyprland/src/output/Monitor.hpp>
#include <hyprland/src/plugins/HookSystem.hpp>

namespace Render {
class CRenderContext;
}

namespace Overview {

bool initializeRendererHooks(HANDLE handle);
void shutdownRendererHooks(HANDLE handle);
void fullRenderMonitor(Render::CRenderContext& ctx, PHLMONITOR monitor);

} // namespace Overview
