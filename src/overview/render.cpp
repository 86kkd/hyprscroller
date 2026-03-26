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
#include <chrono>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/event/EventBus.hpp>
#include <hyprland/src/helpers/Color.hpp>
#include <hyprland/src/plugins/PluginAPI.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprland/src/render/pass/RectPassElement.hpp>
#include <hyprland/src/render/pass/TexPassElement.hpp>
#include <spdlog/spdlog.h>

#include "session.h"

namespace Overview {
namespace {

using RenderWorkspaceHookFn = void (*)(void* renderer, PHLMONITOR monitor, PHLWORKSPACE workspace, const Time::steady_tp& now, const CBox& geometry);

CFunctionHook*          g_renderWorkspaceHook = nullptr;
RenderWorkspaceHookFn   g_originalRenderWorkspace = nullptr;
CHyprSignalListener     g_renderPreListener = nullptr;
std::unordered_set<int> g_renderedMonitors;
std::unordered_map<WORKSPACEID, CFramebuffer> g_previewBuffers;
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

void update_preview_texture(PHLMONITOR monitor, const WorkspaceNode& previewWorkspace) {
    if (!monitor || !g_originalRenderWorkspace)
        return;

    const auto targetWorkspace = g_pCompositor->getWorkspaceByID(previewWorkspace.workspaceId);
    if (!targetWorkspace)
        return;

    const auto width = std::max(1, static_cast<int>(std::lround(previewWorkspace.box.w)));
    const auto height = std::max(1, static_cast<int>(std::lround(previewWorkspace.box.h)));
    auto& previewBuffer = g_previewBuffers[previewWorkspace.workspaceId];

    if (!previewBuffer.isAllocated() || static_cast<int>(previewBuffer.m_size.x) != width ||
        static_cast<int>(previewBuffer.m_size.y) != height) {
        previewBuffer.release();
        if (!previewBuffer.alloc(width, height)) {
            spdlog::warn("overview_preview_alloc_failed: workspace={} size=({}, {})", previewWorkspace.workspaceId, width, height);
            return;
        }
    }

    CRegion damage(CBox(0, 0, width, height));
    g_renderingOverview = true;
    if (g_pHyprRenderer->beginRender(monitor, damage, RENDER_MODE_NORMAL, {}, &previewBuffer, true)) {
        g_originalRenderWorkspace(g_pHyprRenderer.get(), monitor, targetWorkspace, std::chrono::steady_clock::now(), CBox(0, 0, width, height));
        g_pHyprRenderer->endRender();
    }
    g_renderingOverview = false;
}

void update_preview_textures_for_monitor(PHLMONITOR monitor) {
    const auto* region = region_for_monitor(session().monitors(), monitor);
    if (!region)
        return;

    for (const auto& previewWorkspace : region->workspaces)
        update_preview_texture(monitor, previewWorkspace);
}

void compose_preview_texture(PHLMONITOR monitor, const WorkspaceNode& previewWorkspace) {
    const auto previewBufferIt = g_previewBuffers.find(previewWorkspace.workspaceId);
    if (previewBufferIt == g_previewBuffers.end())
        return;

    const auto texture = previewBufferIt->second.getTexture();
    if (!texture)
        return;

    CTexPassElement::SRenderData data;
    data.tex = texture;
    data.box = to_cbox(previewWorkspace.box);
    data.a = 1.0F;
    data.blurA = 1.0F;
    data.damage = CRegion(data.box);
    data.round = 18;
    data.roundingPower = 2.0F;
    data.flipEndFrame = false;
    data.clipBox = CBox(monitor->m_position.x, monitor->m_position.y, monitor->m_size.x, monitor->m_size.y);
    data.blur = false;
    g_pHyprRenderer->m_renderPass.add(makeUnique<CTexPassElement>(data));
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
    for (const auto& previewWorkspace : region->workspaces)
        compose_preview_texture(monitor, previewWorkspace);

    draw_selection_overlay(monitor);
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
        if (session().active())
            update_preview_textures_for_monitor(monitor);
    });

    spdlog::info("overview_renderer_init: hooked renderWorkspace address={} matches={}",
                 it->address,
                 matches.size());
    return true;
}

void shutdownRendererHooks(HANDLE handle) {
    g_renderPreListener = nullptr;
    g_renderedMonitors.clear();
    g_previewBuffers.clear();
    g_originalRenderWorkspace = nullptr;
    g_renderingOverview = false;

    if (!g_renderWorkspaceHook)
        return;

    HyprlandAPI::removeFunctionHook(handle, g_renderWorkspaceHook);
    g_renderWorkspaceHook = nullptr;
    spdlog::info("overview_renderer_shutdown");
}

} // namespace Overview
