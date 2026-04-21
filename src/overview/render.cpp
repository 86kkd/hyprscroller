/**
 * @file render.cpp
 * @brief Stable overview renderer built on top of Hyprland render passes.
 *
 * The overview renderer intentionally avoids symbol scanning and any mutation
 * of real window geometry. It draws a monitor-local overlay from the read-only
 * overview session model without turning overview into an editing mode.
 */
#include "render.h"

#include <chrono>

#include "render_draw.h"
#include "render_hooks.h"
#include "render_state.h"
#include "session.h"

namespace Overview {

void fullRenderMonitor(PHLMONITOR monitor) {
    if (!monitor || !session().active())
        return;

    const auto* scene = renderState().sceneForMonitor(monitor->m_id);
    if (!scene)
        return;

    drawSceneMonitor(*scene, std::chrono::steady_clock::now(), renderState());
}

bool initializeRendererHooks(HANDLE handle) {
    return initializeRendererHooksImpl(handle);
}

void shutdownRendererHooks(HANDLE handle) {
    shutdownRendererHooksImpl(handle);
}

} // namespace Overview
