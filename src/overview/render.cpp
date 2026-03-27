/**
 * @file render.cpp
 * @brief Overview preview rendering via a dedicated overview pass element.
 */
#include "render.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/event/EventBus.hpp>
#include <hyprland/src/helpers/Color.hpp>
#include <hyprland/src/plugins/PluginAPI.hpp>
#include <hyprland/src/render/OpenGL.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <spdlog/spdlog.h>

#include "monitor_space.h"
#include "pass_element.h"
#include "session.h"

namespace Overview {
namespace {

using RenderWorkspaceHookFn = void (*)(void* renderer, PHLMONITOR monitor, PHLWORKSPACE workspace, const Time::steady_tp& now, const CBox& geometry);
using RenderWindowFn = void (*)(void* renderer, PHLWINDOW window, PHLMONITOR monitor, const Time::steady_tp& now, bool decorate, eRenderPassMode pass, bool ignorePosition, bool standalone);
using RenderTextureInternalFn = void (*)(void* opengl, SP<CTexture> texture, const CBox& box, const CHyprOpenGLImpl::STextureRenderData& data);

struct WindowPreview {
    CFramebuffer fb;
    PHLWINDOW    window = nullptr;
    Vector2D     originalPos;
    Vector2D     originalSize;
    Vector2D     capturedSize;
};

CFunctionHook*                        g_renderWorkspaceHook = nullptr;
RenderWorkspaceHookFn                 g_originalRenderWorkspace = nullptr;
RenderWindowFn                        g_renderWindow = nullptr;
RenderTextureInternalFn               g_renderTextureInternal = nullptr;
CHyprSignalListener                   g_renderPreListener = nullptr;
std::unordered_set<int>               g_renderedMonitors;
std::unordered_set<int>               g_dirtyMonitors;
std::unordered_map<void*, WindowPreview> g_windowPreviews;
std::unordered_map<std::string, Time::steady_tp> g_logTimestamps;
std::unordered_map<int, Time::steady_tp> g_overviewOpenedAt;
bool                                  g_renderingOverview = false;
bool                                  g_lastOverviewActive = false;

void* preview_key(PHLWINDOW window) {
    return window ? static_cast<void*>(window.get()) : nullptr;
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

bool should_log(const std::string& key, std::chrono::milliseconds interval = std::chrono::milliseconds(250)) {
    const auto now = std::chrono::steady_clock::now();
    const auto it = g_logTimestamps.find(key);
    if (it != g_logTimestamps.end() && now - it->second < interval)
        return false;

    g_logTimestamps[key] = now;
    return true;
}

CBox preview_target_box(PHLMONITOR monitor, const Target& target) {
    const auto renderSpace = MonitorSpace::fromMonitor(monitor);
    auto       targetBox = renderSpace.toLocalBox(target.box);
    if (target.type != TargetType::Window || !target.window)
        return targetBox;

    const auto it = g_windowPreviews.find(preview_key(target.window));
    if (it == g_windowPreviews.end())
        return targetBox;

    const ScrollerCore::Box sourceBoxLogical{
        it->second.originalPos.x,
        it->second.originalPos.y,
        it->second.originalSize.x,
        it->second.originalSize.y,
    };
    return renderSpace.toLocalBox(sourceBoxLogical);
}

void draw_monitor_backdrop(PHLMONITOR monitor) {
    if (!monitor)
        return;

    g_pHyprOpenGL->clear(CHyprColor(0.08F, 0.09F, 0.12F, 1.0F));
}

void draw_selection_overlay(PHLMONITOR monitor) {
    if (!monitor)
        return;

    const auto& activeSession = session();
    const auto* selection = activeSession.model().selection();
    if (!selection || selection->monitorId != monitor->m_id)
        return;

    const auto box = preview_target_box(monitor, *selection);

    CHyprOpenGLImpl::SRectRenderData data;
    data.round = static_cast<int>(std::round(18.0 * monitor->m_scale));
    data.roundingPower = 2.0F;
    g_pHyprOpenGL->renderRect(box,
                              selection->synthetic ? CHyprColor(0.18F, 0.72F, 0.98F, 0.20F)
                                                   : CHyprColor(0.98F, 0.74F, 0.18F, 0.16F),
                              data);
}

struct ScopedOverviewRendering {
    ScopedOverviewRendering() {
        g_renderingOverview = true;
    }

    ~ScopedOverviewRendering() {
        g_renderingOverview = false;
    }
};

struct ScopedPreviewCapture {
    ScopedPreviewCapture(PHLMONITOR monitor, PHLWINDOW window, const MonitorSpace& captureSpace)
        : window_(window),
          previousWorkspace_(window ? window->m_workspace : nullptr),
          previousRealPosition_(window ? window->m_realPosition->value() : Vector2D{}),
          previousRealSize_(window ? window->m_realSize->value() : Vector2D{}),
          previousBlockSurfaceFeedback_(g_pHyprRenderer->m_bBlockSurfaceFeedback),
          uprightCaptureGuard_(monitor) {
        if (!window_)
            return;

        g_pHyprRenderer->m_bBlockSurfaceFeedback = true;
        if (monitor->m_activeWorkspace)
            window_->m_workspace = monitor->m_activeWorkspace;
        window_->m_realPosition->setValue(captureSpace.origin);
    }

    ~ScopedPreviewCapture() {
        restore();
    }

    void restore() {
        if (!window_ || restored_)
            return;

        window_->m_realPosition->setValue(previousRealPosition_);
        window_->m_workspace = previousWorkspace_;
        g_pHyprRenderer->m_bBlockSurfaceFeedback = previousBlockSurfaceFeedback_;
        restored_ = true;
    }

    const Vector2D& previousRealPosition() const {
        return previousRealPosition_;
    }

    const Vector2D& previousRealSize() const {
        return previousRealSize_;
    }

  private:
    PHLWINDOW                    window_ = nullptr;
    PHLWORKSPACE                 previousWorkspace_ = nullptr;
    Vector2D                     previousRealPosition_;
    Vector2D                     previousRealSize_;
    bool                         previousBlockSurfaceFeedback_ = false;
    bool                         restored_ = false;
    ScopedUprightMonitorCapture  uprightCaptureGuard_;
};

bool ensure_preview_framebuffer(WindowPreview& preview, PHLMONITOR monitor, const Target& target,
                                int renderWidth, int renderHeight, int contentWidth, int contentHeight) {
    const auto sizeMatches = preview.fb.isAllocated()
        && static_cast<int>(preview.fb.m_size.x) == renderWidth
        && static_cast<int>(preview.fb.m_size.y) == renderHeight;
    if (sizeMatches)
        return true;

    spdlog::debug("overview_window_preview_resize: monitor={} workspace={} window={} size=({}, {}) content=({}, {}) target_box=({}, {}, {}, {}) allocated={}",
                  monitor->m_id,
                  target.workspaceId,
                  preview_key(target.window),
                  renderWidth,
                  renderHeight,
                  contentWidth,
                  contentHeight,
                  target.box.x,
                  target.box.y,
                  target.box.w,
                  target.box.h,
                  preview.fb.isAllocated());
    preview.fb.release();
    if (preview.fb.alloc(renderWidth, renderHeight))
        return true;

    spdlog::warn("overview_window_preview_alloc_failed: workspace={} window={} size=({}, {})",
                 target.workspaceId,
                 preview_key(target.window),
                 renderWidth,
                 renderHeight);
    return false;
}

void log_preview_capture(const char* phase, PHLMONITOR monitor, PHLWINDOW window,
                         const MonitorSpace& renderSpace, const MonitorSpace& captureSpace,
                         const ScopedPreviewCapture& capture, int renderWidth, int renderHeight) {
    const auto afterPhase = std::string_view(phase) == "after";
    const auto key = std::string("overview_window_preview_call")
        + (afterPhase ? "_after_" : "_")
        + std::to_string(monitor->m_id)
        + "_"
        + std::to_string(reinterpret_cast<uintptr_t>(preview_key(window)));
    if (!should_log(key))
        return;

    if (!afterPhase) {
        spdlog::debug("overview_window_preview_call: phase=before monitor={} orientation={} capture_orientation={} transform={} scale={} window={} mapped={} workspace={} active_workspace={} "
                      "monitor_pos=({}, {}) monitor_size=({}, {}) pixel_size=({}, {}) transformed_size=({}, {}) capture_transformed_size=({}, {}) "
                      "window_pos=({}, {}) window_size=({}, {}) capture_pos=({}, {}) capture_size=({}, {}) fb=({}, {})",
                      monitor->m_id,
                      orientation_name(renderSpace.orientation()),
                      orientation_name(captureSpace.orientation()),
                      static_cast<int>(renderSpace.transform),
                      monitor->m_scale,
                      preview_key(window),
                      window->m_isMapped,
                      window->workspaceID(),
                      monitor->m_activeWorkspace ? monitor->m_activeWorkspace->m_id : WORKSPACE_INVALID,
                      monitor->m_position.x,
                      monitor->m_position.y,
                      monitor->m_size.x,
                      monitor->m_size.y,
                      renderSpace.pixelSize.x,
                      renderSpace.pixelSize.y,
                      renderSpace.transformedSize.x,
                      renderSpace.transformedSize.y,
                      captureSpace.transformedSize.x,
                      captureSpace.transformedSize.y,
                      capture.previousRealPosition().x,
                      capture.previousRealPosition().y,
                      capture.previousRealSize().x,
                      capture.previousRealSize().y,
                      window->m_realPosition->value().x,
                      window->m_realPosition->value().y,
                      window->m_realSize->value().x,
                      window->m_realSize->value().y,
                      renderWidth,
                      renderHeight);
        return;
    }

    spdlog::debug("overview_window_preview_call: phase=after monitor={} orientation={} capture_orientation={} transform={} scale={} window={} mapped={} workspace={} active_workspace={} "
                  "window_pos=({}, {}) window_size=({}, {}) capture_pos=({}, {}) capture_size=({}, {}) fb=({}, {})",
                  monitor->m_id,
                  orientation_name(renderSpace.orientation()),
                  orientation_name(captureSpace.orientation()),
                  static_cast<int>(renderSpace.transform),
                  monitor->m_scale,
                  preview_key(window),
                  window->m_isMapped,
                  window->workspaceID(),
                  monitor->m_activeWorkspace ? monitor->m_activeWorkspace->m_id : WORKSPACE_INVALID,
                  capture.previousRealPosition().x,
                  capture.previousRealPosition().y,
                  capture.previousRealSize().x,
                  capture.previousRealSize().y,
                  window->m_realPosition->value().x,
                  window->m_realPosition->value().y,
                  window->m_realSize->value().x,
                  window->m_realSize->value().y,
                  renderWidth,
                  renderHeight);
}

void render_preview_window(PHLMONITOR monitor, PHLWINDOW window) {
    if (!g_renderWindow)
        return;

    g_pHyprOpenGL->pushMonitorTransformEnabled(false);
    g_renderWindow(g_pHyprRenderer.get(), window, monitor, std::chrono::steady_clock::now(), false, RENDER_PASS_MAIN, false, false);
    g_pHyprOpenGL->popMonitorTransformEnabled();
}

void log_preview_rendered(PHLMONITOR monitor, const Target& target, const WindowPreview& preview,
                          int renderWidth, int renderHeight, int contentWidth, int contentHeight) {
    const auto key = "overview_window_preview_rendered_" + std::to_string(monitor->m_id)
        + "_" + std::to_string(reinterpret_cast<uintptr_t>(preview_key(target.window)));
    if (!should_log(key))
        return;

    spdlog::debug("overview_window_preview_rendered: monitor={} workspace={} window={} size=({}, {}) content=({}, {}) original=({}, {}, {}, {})",
                  monitor->m_id,
                  target.workspaceId,
                  preview_key(target.window),
                  renderWidth,
                  renderHeight,
                  contentWidth,
                  contentHeight,
                  preview.originalPos.x,
                  preview.originalPos.y,
                  preview.originalSize.x,
                  preview.originalSize.y);
}

void update_window_preview_texture(PHLMONITOR monitor, const Target& target) {
    if (!monitor || !target.window || target.type != TargetType::Window)
        return;

    auto window = target.window;
    if (!window || !window->m_isMapped)
        return;

    const auto renderSize = window->m_realSize->value();
    const auto contentWidth = std::max(1, static_cast<int>(std::round(renderSize.x * monitor->m_scale)));
    const auto contentHeight = std::max(1, static_cast<int>(std::round(renderSize.y * monitor->m_scale)));
    const auto renderWidth = contentWidth;
    const auto renderHeight = contentHeight;
    const auto renderSpace = MonitorSpace::fromMonitor(monitor);
    const auto captureSpace = MonitorSpace::uprightCapture(monitor);
    auto& preview = g_windowPreviews[preview_key(window)];
    preview.window = window;
    preview.originalPos = window->m_realPosition->value();
    preview.originalSize = window->m_realSize->value();
    preview.capturedSize = Vector2D(contentWidth, contentHeight);

    if (!ensure_preview_framebuffer(preview, monitor, target, renderWidth, renderHeight, contentWidth, contentHeight))
        return;

    CRegion damage(CBox(0, 0, renderWidth, renderHeight));
    ScopedOverviewRendering overviewGuard;
    if (!g_pHyprRenderer->beginRender(monitor, damage, RENDER_MODE_FULL_FAKE, nullptr, &preview.fb)) {
        spdlog::warn("overview_window_preview_begin_render_failed: monitor={} workspace={} window={} size=({}, {})",
                     monitor->m_id,
                     target.workspaceId,
                     preview_key(window),
                     renderWidth,
                     renderHeight);
        return;
    }

    g_pHyprOpenGL->clear(CHyprColor(0.F, 0.F, 0.F, 0.F));
    ScopedPreviewCapture capture(monitor, window, captureSpace);
    log_preview_capture("before", monitor, window, renderSpace, captureSpace, capture, renderWidth, renderHeight);
    render_preview_window(monitor, window);
    g_pHyprOpenGL->m_renderData.blockScreenShader = true;
    log_preview_capture("after", monitor, window, renderSpace, captureSpace, capture, renderWidth, renderHeight);
    capture.restore();
    g_pHyprRenderer->endRender();
    log_preview_rendered(monitor, target, preview, renderWidth, renderHeight, contentWidth, contentHeight);
}

void update_preview_textures_for_monitor(PHLMONITOR monitor) {
    const auto* region = region_for_monitor(session().model().monitors(), monitor);
    const auto renderSpace = MonitorSpace::fromMonitor(monitor);
    if (!region) {
        if (should_log("overview_preview_no_region_" + std::to_string(monitor ? monitor->m_id : -1), std::chrono::milliseconds(500))) {
            spdlog::debug("overview_preview_update_skip: monitor={} reason=no_region",
                          monitor ? monitor->m_id : -1);
        }
        return;
    }

    if (should_log("overview_preview_update_" + std::to_string(monitor->m_id))) {
        spdlog::debug("overview_preview_update: monitor={} orientation={} workspaces={} region_box=({}, {}, {}, {}) pixel_size=({}, {}) transformed_size=({}, {})",
                      monitor->m_id,
                      orientation_name(renderSpace.orientation()),
                      region->workspaces.size(),
                      region->box.x,
                      region->box.y,
                      region->box.w,
                      region->box.h,
                      renderSpace.pixelSize.x,
                      renderSpace.pixelSize.y,
                      renderSpace.transformedSize.x,
                      renderSpace.transformedSize.y);
    }

    for (const auto& workspace : region->workspaces) {
        for (const auto& target : workspace.targets)
            update_window_preview_texture(monitor, target);
    }
}

void compose_empty_target(PHLMONITOR monitor, const Target& target) {
    const auto box = preview_target_box(monitor, target);

    CHyprOpenGLImpl::SRectRenderData data;
    data.round = static_cast<int>(std::round(18.0 * monitor->m_scale));
    data.roundingPower = 2.0F;
    g_pHyprOpenGL->renderRect(box,
                              target.synthetic ? CHyprColor(0.12F, 0.32F, 0.44F, 0.35F)
                                               : CHyprColor(0.10F, 0.12F, 0.16F, 0.55F),
                              data);
}

[[maybe_unused]] void compose_window_live_preview(PHLMONITOR monitor, const Target& target) {
    if (!monitor || !target.window || !g_renderWindow)
        return;

    auto window = target.window;
    const auto originalSize = window->m_realSize->value();
    if (originalSize.x <= 0.0 || originalSize.y <= 0.0)
        return;

    const auto box = preview_target_box(monitor, target);
    const auto currentWorkspace = window->m_workspace;
    const auto currentFullscreen = window->m_fullscreenState;
    const auto currentPinned = window->m_pinned;
    const auto currentFloating = window->m_isFloating;
    const auto renderScale = box.w / std::max(1.0, originalSize.x * monitor->m_scale);
    SRenderModifData renderModif;
    renderModif.modifs.push_back(
        {SRenderModifData::eRenderModifType::RMOD_TYPE_TRANSLATE,
         (monitor->m_position * monitor->m_scale) + (box.pos() / renderScale) - (window->m_realPosition->value() * monitor->m_scale)});
    renderModif.modifs.push_back({SRenderModifData::eRenderModifType::RMOD_TYPE_SCALE, renderScale});
    renderModif.enabled = true;

    if (monitor->m_activeWorkspace)
        window->m_workspace = monitor->m_activeWorkspace;
    window->m_fullscreenState = Desktop::View::SFullscreenState{FSMODE_NONE};
    window->m_isFloating = false;
    window->m_pinned = true;
    window->m_ruleApplicator->nearestNeighbor().set(false, Desktop::Types::PRIORITY_SET_PROP);

    const auto previousRenderModif = g_pHyprOpenGL->m_renderData.renderModif;
    g_pHyprOpenGL->m_renderData.renderModif = renderModif;
    g_pHyprOpenGL->setRenderModifEnabled(true);
    g_renderWindow(g_pHyprRenderer.get(), window, monitor, std::chrono::steady_clock::now(), true, RENDER_PASS_ALL, false, false);
    g_pHyprOpenGL->m_renderData.renderModif = previousRenderModif;
    g_pHyprOpenGL->setRenderModifEnabled(previousRenderModif.enabled);

    window->m_workspace = currentWorkspace;
    window->m_fullscreenState = currentFullscreen;
    window->m_isFloating = currentFloating;
    window->m_pinned = currentPinned;
    window->m_ruleApplicator->nearestNeighbor().unset(Desktop::Types::PRIORITY_SET_PROP);
}

void compose_window_preview(PHLMONITOR monitor, const Target& target) {
    const auto previewIt = g_windowPreviews.find(preview_key(target.window));
    if (previewIt == g_windowPreviews.end()) {
        if (should_log("overview_window_compose_no_buffer_" + std::to_string(reinterpret_cast<uintptr_t>(preview_key(target.window))))) {
            spdlog::debug("overview_compose_skip: monitor={} workspace={} window={} reason=no_buffer",
                          monitor ? monitor->m_id : -1,
                          target.workspaceId,
                          preview_key(target.window));
        }
        return;
    }

    const auto texture = previewIt->second.fb.getTexture();
    if (!texture) {
        if (should_log("overview_window_compose_no_texture_" + std::to_string(reinterpret_cast<uintptr_t>(preview_key(target.window))))) {
            spdlog::debug("overview_compose_skip: monitor={} workspace={} window={} reason=no_texture buffer_size=({}, {})",
                          monitor ? monitor->m_id : -1,
                          target.workspaceId,
                          preview_key(target.window),
                          previewIt->second.fb.m_size.x,
                          previewIt->second.fb.m_size.y);
        }
        return;
    }

    const auto renderSpace = MonitorSpace::fromMonitor(monitor);
    const auto targetLocalBox = renderSpace.toLocalBox(target.box);
    const auto box = preview_target_box(monitor, target);
    CRegion damage{0, 0, INT16_MAX, INT16_MAX};
    const ScrollerCore::Box sourceLogicalBox{
        previewIt->second.originalPos.x,
        previewIt->second.originalPos.y,
        previewIt->second.originalSize.x,
        previewIt->second.originalSize.y,
    };
    const auto sourceLocalBox = renderSpace.toLocalBox(sourceLogicalBox);

    CHyprOpenGLImpl::STextureRenderData data;
    data.damage = &damage;
    data.a = 1.0F;
    data.round = static_cast<int>(std::round(18.0 * monitor->m_scale));
    data.roundingPower = 2.0F;
    data.blockBlurOptimization = true;
    if (monitor && should_log("overview_window_compose_" + std::to_string(monitor->m_id) + "_" +
                              std::to_string(reinterpret_cast<uintptr_t>(preview_key(target.window))))) {
        spdlog::debug("overview_window_compose: monitor={} orientation={} transform={} scale={} window={} monitor_pos=({}, {}) monitor_size=({}, {}) pixel_size=({}, {}) transformed_size=({}, {}) "
                      "target_global=({}, {}, {}, {}) target_local=({}, {}, {}, {}) draw_box=({}, {}, {}, {}) "
                      "source_global=({}, {}, {}, {}) source_local=({}, {}, {}, {}) "
                      "fb=({}, {}) content=({}, {})",
                      monitor->m_id,
                      orientation_name(renderSpace.orientation()),
                      static_cast<int>(renderSpace.transform),
                      monitor->m_scale,
                      preview_key(target.window),
                      renderSpace.origin.x,
                      renderSpace.origin.y,
                      renderSpace.logicalSize.x,
                      renderSpace.logicalSize.y,
                      renderSpace.pixelSize.x,
                      renderSpace.pixelSize.y,
                      renderSpace.transformedSize.x,
                      renderSpace.transformedSize.y,
                      target.box.x,
                      target.box.y,
                      target.box.w,
                      target.box.h,
                      targetLocalBox.x,
                      targetLocalBox.y,
                      targetLocalBox.w,
                      targetLocalBox.h,
                      box.x,
                      box.y,
                      box.w,
                      box.h,
                      sourceLogicalBox.x,
                      sourceLogicalBox.y,
                      sourceLogicalBox.w,
                      sourceLogicalBox.h,
                      sourceLocalBox.x,
                      sourceLocalBox.y,
                      sourceLocalBox.w,
                      sourceLocalBox.h,
                      previewIt->second.fb.m_size.x,
                      previewIt->second.fb.m_size.y,
                      previewIt->second.capturedSize.x,
                      previewIt->second.capturedSize.y);
    }
    if (g_renderTextureInternal)
        g_renderTextureInternal(g_pHyprOpenGL.get(), texture, box, data);
    else
        g_pHyprOpenGL->renderTexture(texture, box, data);
}

void render_monitor_overview(PHLMONITOR monitor, const MonitorRegion* region) {
    draw_monitor_backdrop(monitor);

    if (region) {
        for (const auto& workspace : region->workspaces) {
            for (const auto& target : workspace.targets) {
                if (target.type == TargetType::Window)
                    compose_window_preview(monitor, target);
                else
                    compose_empty_target(monitor, target);
            }
        }
    }

    draw_selection_overlay(monitor);
}

void hkRenderWorkspace(void* renderer, PHLMONITOR monitor, PHLWORKSPACE workspace, const Time::steady_tp& now, const CBox& geometry) {
    if (!g_originalRenderWorkspace)
        return;

    if (g_renderingOverview || !session().active() || !monitor) {
        if (monitor && should_log("overview_render_passthrough_" + std::to_string(monitor->m_id), std::chrono::milliseconds(500))) {
            spdlog::debug("overview_render_passthrough: monitor={} workspace={} reason={}",
                          monitor->m_id,
                          workspace ? workspace->m_id : WORKSPACE_INVALID,
                          g_renderingOverview ? "recursive" : (!session().active() ? "inactive" : "no_monitor"));
        }
        g_originalRenderWorkspace(renderer, monitor, workspace, now, geometry);
        return;
    }

    const auto* region = region_for_monitor(session().model().monitors(), monitor);

    if (g_renderedMonitors.contains(monitor->m_id))
        return;

    if (should_log("overview_render_compose_" + std::to_string(monitor->m_id))) {
        const auto renderSpace = MonitorSpace::fromMonitor(monitor);
        spdlog::debug("overview_render_compose: monitor={} orientation={} trigger_workspace={} previews={} geometry=({}, {}, {}, {}) pixel_size=({}, {}) transformed_size=({}, {})",
                      monitor->m_id,
                      orientation_name(renderSpace.orientation()),
                      workspace ? workspace->m_id : WORKSPACE_INVALID,
                      region ? region->workspaces.size() : 0,
                      geometry.x,
                      geometry.y,
                      geometry.w,
                      geometry.h,
                      renderSpace.pixelSize.x,
                      renderSpace.pixelSize.y,
                      renderSpace.transformedSize.x,
                      renderSpace.transformedSize.y);
    }

    g_renderedMonitors.insert(monitor->m_id);
    if (!region) {
        g_originalRenderWorkspace(renderer, monitor, workspace, now, geometry);
        return;
    }

    g_pHyprRenderer->m_renderPass.add(makeUnique<OverviewPassElement>(monitor));
}

} // namespace

void fullRenderMonitor(PHLMONITOR monitor) {
    if (!monitor)
        return;

    render_monitor_overview(monitor, region_for_monitor(session().model().monitors(), monitor));
}

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

    const auto renderWindowMatches = HyprlandAPI::findFunctionsByName(handle, "renderWindow");
    const auto renderWindowIt = std::find_if(renderWindowMatches.begin(), renderWindowMatches.end(), [](const SFunctionMatch& match) {
        return match.demangled.find("CHyprRenderer::renderWindow") != std::string::npos;
    });
    if (renderWindowIt == renderWindowMatches.end()) {
        spdlog::warn("overview_renderer_init: renderWindow symbol not found");
        return false;
    }

    const auto renderTextureInternalMatches = HyprlandAPI::findFunctionsByName(handle, "renderTextureInternal");
    const auto renderTextureInternalIt = std::find_if(renderTextureInternalMatches.begin(),
                                                      renderTextureInternalMatches.end(),
                                                      [](const SFunctionMatch& match) {
                                                          return match.demangled.find("CHyprOpenGLImpl::renderTextureInternal") != std::string::npos;
                                                      });
    if (renderTextureInternalIt == renderTextureInternalMatches.end()) {
        spdlog::warn("overview_renderer_init: renderTextureInternal symbol not found");
    } else {
        g_renderTextureInternal = reinterpret_cast<RenderTextureInternalFn>(renderTextureInternalIt->address);
    }

    g_originalRenderWorkspace = reinterpret_cast<RenderWorkspaceHookFn>(g_renderWorkspaceHook->m_original);
    g_renderWindow = reinterpret_cast<RenderWindowFn>(renderWindowIt->address);
    g_renderPreListener = Event::bus()->m_events.render.pre.listen([](PHLMONITOR monitor) {
        if (!monitor)
            return;

        const auto overviewActive = session().active();
        if (overviewActive != g_lastOverviewActive) {
            g_logTimestamps.clear();
            g_dirtyMonitors.clear();
            g_overviewOpenedAt.clear();
            if (overviewActive) {
                const auto openedAt = std::chrono::steady_clock::now();
                for (const auto& region : session().model().monitors()) {
                    g_dirtyMonitors.insert(region.monitorId);
                    g_overviewOpenedAt[region.monitorId] = openedAt;
                }
            }
            spdlog::debug("overview_render_session_state: active={} monitors={}",
                          overviewActive,
                          session().model().monitors().size());
            g_lastOverviewActive = overviewActive;
        }

        g_renderedMonitors.erase(monitor->m_id);
        if (!overviewActive)
            return;

        if (!g_dirtyMonitors.contains(monitor->m_id))
            return;

        update_preview_textures_for_monitor(monitor);
        g_dirtyMonitors.erase(monitor->m_id);
    });

    spdlog::info("overview_renderer_init: hooked renderWorkspace address={} matches={}",
                 it->address,
                 matches.size());
    return true;
}

void shutdownRendererHooks(HANDLE handle) {
    g_renderPreListener = nullptr;
    g_renderedMonitors.clear();
    g_dirtyMonitors.clear();
    g_windowPreviews.clear();
    g_logTimestamps.clear();
    g_overviewOpenedAt.clear();
    g_originalRenderWorkspace = nullptr;
    g_renderWindow = nullptr;
    g_renderTextureInternal = nullptr;
    g_renderingOverview = false;
    g_lastOverviewActive = false;
    g_pHyprRenderer->m_renderPass.removeAllOfType("OverviewPassElement");

    if (!g_renderWorkspaceHook)
        return;

    HyprlandAPI::removeFunctionHook(handle, g_renderWorkspaceHook);
    g_renderWorkspaceHook = nullptr;
    spdlog::info("overview_renderer_shutdown");
}

} // namespace Overview
