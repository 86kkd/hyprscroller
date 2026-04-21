/**
 * @file render_hooks.cpp
 * @brief Internal overview render hook bootstrap.
 */
#include "render_hooks.h"

#include <chrono>
#include <exception>

#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/event/EventBus.hpp>
#include <hyprland/src/render/OpenGL.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <spdlog/spdlog.h>

#include "pass_element.h"
#include "render_draw.h"
#include "render_state.h"
#include "scene.h"
#include "session.h"

namespace Overview {
namespace {

CHyprSignalListener g_renderPreListener = nullptr;
CHyprSignalListener g_renderStageListener = nullptr;
CHyprSignalListener g_keyboardKeyListener = nullptr;

void damage_monitor_if_animating(PHLMONITOR monitor, steady_tp now) {
    if (!monitor)
        return;

    auto& state = renderState();
    if (state.openingAnimationActive(monitor->m_id, now, session().active())) {
        g_pHyprRenderer->damageMonitor(monitor);
        return;
    }

    if (state.selectionAnimationActive(monitor->m_id, now))
        g_pHyprRenderer->damageMonitor(monitor);
}

void handle_session_transition(steady_tp now) {
    auto& overview = session();
    auto& state = renderState();
    const auto overviewActive = overview.active();
    if (overviewActive == state.lastOverviewActive())
        return;

    if (overviewActive) {
        state.clearSessionState();
        snapshotWindowTargets(overview.model());
        snapshotBackdropLayers(overview.model(), state);
        for (const auto& region : overview.model().monitors()) {
            state.markOpened(region.monitorId, now);
            if (region.monitor)
                g_pHyprRenderer->damageMonitor(region.monitor);
        }
        spdlog::info("overview_renderer: enabled monitors={}", overview.model().monitors().size());
    } else {
        for (const auto& [monitorId, _scene] : state.scenes()) {
            if (const auto monitor = g_pCompositor->getMonitorFromID(monitorId))
                g_pHyprRenderer->damageMonitor(monitor);
        }
        state.clearSessionState();
        spdlog::info("overview_renderer: disabled monitors=0");
    }

    state.setLastOverviewActive(overviewActive);
}

void update_live_scene_for_monitor(PHLMONITOR monitor, steady_tp now) {
    if (!monitor)
        return;

    auto& state = renderState();
    const auto scene = buildSceneForMonitor(monitor, session().model());
    if (!scene) {
        state.eraseScene(monitor->m_id);
        return;
    }

    state.updateSelectionPulse(*scene, now);
    state.setScene(monitor->m_id, *scene);
}

} // namespace

bool initializeRendererHooksImpl(HANDLE handle) {
    (void)handle;
    if (g_renderPreListener || g_renderStageListener || g_keyboardKeyListener)
        return true;

    try {
        g_renderPreListener = Event::bus()->m_events.render.pre.listen([](PHLMONITOR monitor) {
            if (!monitor)
                return;

            const auto now = std::chrono::steady_clock::now();
            handle_session_transition(now);

            if (session().active())
                update_live_scene_for_monitor(monitor, now);

            damage_monitor_if_animating(monitor, now);
        });

        g_renderStageListener = Event::bus()->m_events.render.stage.listen([](eRenderStage stage) {
            if (stage != RENDER_POST_WINDOWS)
                return;

            const auto monitor = g_pHyprOpenGL->m_renderData.pMonitor.lock();
            if (!monitor || !session().active())
                return;

            if (renderState().sceneForMonitor(monitor->m_id)) {
                enqueueMonitorBackdrop(monitor, renderState());
                g_pHyprRenderer->m_renderPass.add(makeUnique<OverviewPassElement>(monitor));
            }
        });

        g_keyboardKeyListener = Event::bus()->m_events.input.keyboard.key.listen([](IKeyboard::SKeyEvent event, Event::SCallbackInfo& info) {
            (void)info;
            auto& overview = session();
            const auto handledByOverview = overview.consumeInputHandled();

            if (!overview.active() || event.state != WL_KEYBOARD_KEY_STATE_RELEASED)
                return;

            if (handledByOverview)
                return;

            overview.dismiss();
        });

        spdlog::info("overview_renderer_init: using render-pass overlay backend");
        return true;
    } catch (const std::exception& e) {
        g_renderPreListener = nullptr;
        g_renderStageListener = nullptr;
        g_keyboardKeyListener = nullptr;
        renderState().clearAll();
        spdlog::warn("overview_renderer_init: disabled after exception: {}", e.what());
        return false;
    }
}

void shutdownRendererHooksImpl(HANDLE handle) {
    (void)handle;
    g_renderPreListener = nullptr;
    g_renderStageListener = nullptr;
    g_keyboardKeyListener = nullptr;
    renderState().clearAll();
    g_pHyprRenderer->m_renderPass.removeAllOfType("OverviewPassElement");
    spdlog::info("overview_renderer_shutdown");
}

} // namespace Overview
