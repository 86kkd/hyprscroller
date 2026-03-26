/**
 * @file render.cpp
 * @brief Overview preview rendering via Hyprland internal render hook.
 *
 * This follows the same broad integration point as the official `hyprexpo`
 * plugin: hook `renderWorkspace`, then fan out one monitor workspace render
 * into multiple preview renders when the global overview session is active.
 */
#include "render.h"

#include <algorithm>
#include <memory>
#include <string>
#include <unordered_set>

#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/event/EventBus.hpp>
#include <hyprland/src/helpers/Color.hpp>
#include <hyprland/src/plugins/PluginAPI.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprland/src/render/pass/RectPassElement.hpp>
#include <spdlog/spdlog.h>

#include "session.h"

namespace Overview {
namespace {

using RenderWorkspaceHookFn = void (*)(void* renderer, PHLMONITOR monitor, PHLWORKSPACE workspace, const Time::steady_tp& now, const CBox& geometry);

CFunctionHook*          g_renderWorkspaceHook = nullptr;
RenderWorkspaceHookFn   g_originalRenderWorkspace = nullptr;
CHyprSignalListener     g_renderPreListener = nullptr;
std::unordered_set<int> g_renderedMonitors;
bool                    g_renderingOverview = false;

CBox to_cbox(const ScrollerCore::Box& box) {
    return {box.x, box.y, box.w, box.h};
}

const MonitorRegion* region_for_monitor(const std::vector<MonitorRegion>& monitors, PHLMONITOR monitor) {
    if (!monitor)
        return nullptr;

    for (const auto& region : monitors) {
        if (region.monitorId == monitor->m_id)
            return &region;
    }

    return nullptr;
}

void draw_selection_overlay(PHLMONITOR monitor) {
    if (!monitor)
        return;

    const auto& activeSession = session();
    const auto& selection = activeSession.selection();
    if (!selection || selection->monitorId != monitor->m_id)
        return;

    CRectPassElement::SRectData data;
    data.box = to_cbox(selection->box);
    data.color = selection->synthetic ? CHyprColor(0.18F, 0.72F, 0.98F, 0.20F) : CHyprColor(0.98F, 0.74F, 0.18F, 0.16F);
    data.round = 18;
    data.roundingPower = 2.0F;
    data.blur = false;
    data.xray = false;
    data.blurA = 1.0F;
    data.clipBox = CBox(monitor->m_position.x, monitor->m_position.y, monitor->m_size.x, monitor->m_size.y);
    g_pHyprRenderer->m_renderPass.add(makeUnique<CRectPassElement>(data));
}

void hkRenderWorkspace(void* renderer, PHLMONITOR monitor, PHLWORKSPACE workspace, const Time::steady_tp& now, const CBox& geometry) {
    if (!g_originalRenderWorkspace)
        return;

    if (g_renderingOverview || !session().active() || !monitor) {
        g_originalRenderWorkspace(renderer, monitor, workspace, now, geometry);
        return;
    }

    const auto* region = region_for_monitor(session().monitors(), monitor);
    if (!region || region->workspaces.empty()) {
        g_originalRenderWorkspace(renderer, monitor, workspace, now, geometry);
        draw_selection_overlay(monitor);
        return;
    }

    if (g_renderedMonitors.contains(monitor->m_id))
        return;

    g_renderedMonitors.insert(monitor->m_id);
    g_renderingOverview = true;

    for (const auto& previewWorkspace : region->workspaces) {
        const auto targetWorkspace = g_pCompositor->getWorkspaceByID(previewWorkspace.workspaceId);
        if (!targetWorkspace)
            continue;

        g_originalRenderWorkspace(renderer, monitor, targetWorkspace, now, to_cbox(previewWorkspace.box));
    }

    draw_selection_overlay(monitor);
    g_renderingOverview = false;
}

} // namespace

bool initializeRendererHooks(HANDLE handle) {
    if (g_renderWorkspaceHook)
        return true;

    const auto matches = HyprlandAPI::findFunctionsByName(handle, "renderWorkspace");
    const auto it = std::find_if(matches.begin(), matches.end(), [](const SFunctionMatch& match) {
        return match.demangled.find("renderWorkspace") != std::string::npos;
    });

    if (it == matches.end()) {
        spdlog::warn("overview_renderer_init: renderWorkspace symbol not found");
        return false;
    }

    g_renderWorkspaceHook = HyprlandAPI::createFunctionHook(handle, it->address, reinterpret_cast<void*>(&hkRenderWorkspace));
    if (!g_renderWorkspaceHook) {
        spdlog::warn("overview_renderer_init: createFunctionHook failed");
        return false;
    }

    if (!g_renderWorkspaceHook->hook()) {
        spdlog::warn("overview_renderer_init: hook() failed");
        HyprlandAPI::removeFunctionHook(handle, g_renderWorkspaceHook);
        g_renderWorkspaceHook = nullptr;
        return false;
    }

    g_originalRenderWorkspace = reinterpret_cast<RenderWorkspaceHookFn>(g_renderWorkspaceHook->m_original);
    g_renderPreListener = Event::bus()->m_events.render.pre.listen([](PHLMONITOR monitor) {
        if (!monitor)
            return;

        g_renderedMonitors.erase(monitor->m_id);
    });

    spdlog::info("overview_renderer_init: hooked renderWorkspace address={} matches={}",
                 it->address,
                 matches.size());
    return true;
}

void shutdownRendererHooks(HANDLE handle) {
    g_renderPreListener = nullptr;
    g_renderedMonitors.clear();
    g_originalRenderWorkspace = nullptr;
    g_renderingOverview = false;

    if (!g_renderWorkspaceHook)
        return;

    HyprlandAPI::removeFunctionHook(handle, g_renderWorkspaceHook);
    g_renderWorkspaceHook = nullptr;
    spdlog::info("overview_renderer_shutdown");
}

} // namespace Overview
