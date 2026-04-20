/**
 * @file render.cpp
 * @brief Stable overview renderer built on top of Hyprland render passes.
 *
 * The overview renderer intentionally avoids symbol scanning and any mutation
 * of real window geometry. It draws a monitor-local overlay from the read-only
 * overview session model and can re-render window surfaces into thumbnail cards
 * without turning overview into an editing mode.
 */
#include "render.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <exception>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/desktop/view/LayerSurface.hpp>
#include <hyprland/src/desktop/Workspace.hpp>
#include <hyprland/src/event/EventBus.hpp>
#include <hyprland/src/helpers/Color.hpp>
#include <hyprland/src/plugins/PluginAPI.hpp>
#include <hyprland/src/protocols/LayerShell.hpp>
#include <hyprland/src/render/OpenGL.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <spdlog/spdlog.h>

#include "pass_element.h"
#include "scene.h"
#include "session.h"

namespace Overview {
namespace {

using ScrollerCore::Box;
using steady_tp = std::chrono::steady_clock::time_point;

constexpr auto kOpenDuration = std::chrono::milliseconds(180);
constexpr auto kCloseDuration = std::chrono::milliseconds(180);
constexpr auto kSelectionDuration = std::chrono::milliseconds(120);

struct ClosingScene {
    SceneMonitor scene;
    steady_tp    startedAt;
};

struct SelectionPulse {
    Box      box;
    steady_tp startedAt;
};

CHyprSignalListener                         g_renderPreListener = nullptr;
CHyprSignalListener                         g_renderStageListener = nullptr;
CHyprSignalListener                         g_keyboardKeyListener = nullptr;
std::unordered_map<int, steady_tp>         g_openedAt;
std::unordered_map<int, ClosingScene>      g_closingScenes;
std::unordered_map<int, SceneMonitor>      g_liveScenes;
std::unordered_map<int, std::vector<PHLLSREF>> g_backgroundLayers;
std::unordered_map<int, SelectionPulse>    g_selectionPulses;
std::unordered_map<std::string, SP<CTexture>> g_textCache;
bool                                       g_lastOverviewActive = false;

double clamp01(double value) {
    return std::clamp(value, 0.0, 1.0);
}

double ease_out_cubic(double t) {
    const auto clamped = clamp01(t);
    const auto inv = 1.0 - clamped;
    return 1.0 - inv * inv * inv;
}

int iround(double value) {
    return static_cast<int>(std::lround(value));
}

bool approximately_equal(double a, double b, double epsilon = 0.5) {
    return std::abs(a - b) <= epsilon;
}

bool boxes_match(const Box& a, const Box& b) {
    return approximately_equal(a.x, b.x) && approximately_equal(a.y, b.y)
        && approximately_equal(a.w, b.w) && approximately_equal(a.h, b.h);
}

bool finite_box(const Box& box) {
    return std::isfinite(box.x) && std::isfinite(box.y) && std::isfinite(box.w) && std::isfinite(box.h);
}

Box inset_box(const Box& box, double insetX, double insetY) {
    return {
        box.x + insetX,
        box.y + insetY,
        std::max(1.0, box.w - insetX * 2.0),
        std::max(1.0, box.h - insetY * 2.0),
    };
}

Box center_scale_box(const Box& box, double scale) {
    const auto scaledWidth = box.w * scale;
    const auto scaledHeight = box.h * scale;
    return {
        box.x + (box.w - scaledWidth) * 0.5,
        box.y + (box.h - scaledHeight) * 0.5,
        scaledWidth,
        scaledHeight,
    };
}

std::optional<Box> intersect_box(const Box& box, const Box& bounds) {
    if (!finite_box(box) || !finite_box(bounds))
        return std::nullopt;

    const auto x0 = std::max(box.x, bounds.x);
    const auto y0 = std::max(box.y, bounds.y);
    const auto x1 = std::min(box.x + box.w, bounds.x + bounds.w);
    const auto y1 = std::min(box.y + box.h, bounds.y + bounds.h);
    if (x1 <= x0 || y1 <= y0)
        return std::nullopt;

    return Box{x0, y0, x1 - x0, y1 - y0};
}

CBox to_cbox(const Box& box) {
    return CBox{
        static_cast<double>(iround(box.x)),
        static_cast<double>(iround(box.y)),
        static_cast<double>(std::max(1, iround(box.w))),
        static_cast<double>(std::max(1, iround(box.h))),
    };
}

std::string text_cache_key(const std::string& text, const CHyprColor& color, int pt, int maxWidth, int weight) {
    return text + "|" + std::to_string(color.stripA().getAsHex()) + "|" + std::to_string(pt) + "|" + std::to_string(maxWidth) + "|" + std::to_string(weight);
}

SP<CTexture> get_text_texture(const std::string& text, const CHyprColor& color, int pt, int maxWidth = 0, int weight = 400) {
    if (text.empty())
        return nullptr;

    const auto key = text_cache_key(text, color, pt, maxWidth, weight);
    const auto it = g_textCache.find(key);
    if (it != g_textCache.end())
        return it->second;

    auto texture = g_pHyprOpenGL->renderText(text, color.stripA(), pt, false, "", maxWidth, weight);
    g_textCache.emplace(key, texture);
    return texture;
}

void draw_text(const std::string& text, const Box& box, const Box& bounds, const CHyprColor& color, int pt, int weight = 400) {
    const auto clipped = intersect_box(box, bounds);
    if (!clipped || clipped->w <= 4.0 || clipped->h <= 4.0)
        return;

    auto texture = get_text_texture(text, color, pt, std::max(1, iround(clipped->w)), weight);
    if (!texture || texture->m_size.x <= 0.0 || texture->m_size.y <= 0.0)
        return;

    const auto scale = std::min(clipped->w / texture->m_size.x, clipped->h / texture->m_size.y);
    const auto width = std::max(1.0, texture->m_size.x * scale);
    const auto height = std::max(1.0, texture->m_size.y * scale);
    const Box drawBox{
        clipped->x,
        clipped->y + std::max(0.0, (clipped->h - height) * 0.5),
        width,
        height,
    };
    const auto finalBox = intersect_box(drawBox, bounds);
    if (!finalBox)
        return;

    CHyprOpenGLImpl::STextureRenderData data;
    data.a = static_cast<float>(color.a);
    data.blockBlurOptimization = true;
    g_pHyprOpenGL->renderTexture(texture, to_cbox(*finalBox), data);
}

void draw_rect(const Box& box, const Box& bounds, const CHyprColor& color, int round, float roundingPower = 2.0F) {
    const auto clipped = intersect_box(box, bounds);
    if (!clipped)
        return;

    CHyprOpenGLImpl::SRectRenderData data;
    data.round = round;
    data.roundingPower = roundingPower;
    g_pHyprOpenGL->renderRect(to_cbox(*clipped), color, data);
}

void draw_panel(const Box& box, const Box& bounds, const CHyprColor& border, const CHyprColor& fill, int round, double borderWidth = 2.0) {
    const auto clipped = intersect_box(box, bounds);
    if (!clipped || clipped->w <= 2.0 || clipped->h <= 2.0)
        return;

    draw_rect(*clipped, bounds, border, round);
    draw_rect(inset_box(*clipped, borderWidth, borderWidth), bounds, fill, std::max(0, round - iround(borderWidth)));
}

bool draw_window_snapshot(const SceneTarget& target, const Box& box, const Box& bounds, double overlayAlpha) {
    if (!target.window)
        return false;

    const auto clipped = intersect_box(box, bounds);
    if (!clipped || clipped->w <= 4.0 || clipped->h <= 4.0)
        return false;

    const auto monitor = g_pHyprOpenGL->m_renderData.pMonitor.lock();
    const auto sourceMonitor = g_pCompositor->getMonitorFromID(target.window->monitorID());
    if (!monitor || !sourceMonitor || sourceMonitor->m_id != monitor->m_id)
        return false;

    const auto sourceBox = target.window->getWindowMainSurfaceBox();
    if (sourceBox.w <= 1.0 || sourceBox.h <= 1.0)
        return false;

    const auto sourceLocal = Box{
        sourceBox.x - sourceMonitor->m_position.x,
        sourceBox.y - sourceMonitor->m_position.y,
        sourceBox.w,
        sourceBox.h,
    };
    const auto scale = std::min(clipped->w / std::max(1.0, sourceLocal.w),
                                clipped->h / std::max(1.0, sourceLocal.h));
    if (!std::isfinite(scale) || scale <= 0.0)
        return false;

    const auto centeredTarget = Box{
        clipped->x + (clipped->w - sourceLocal.w * scale) * 0.5,
        clipped->y + (clipped->h - sourceLocal.h * scale) * 0.5,
        sourceLocal.w * scale,
        sourceLocal.h * scale,
    };
    const auto translate = Vector2D(centeredTarget.x - sourceLocal.x * scale,
                                    centeredTarget.y - sourceLocal.y * scale);

    g_pHyprOpenGL->saveMatrix();
    g_pHyprOpenGL->setMatrixScaleTranslate(translate, static_cast<float>(scale));
    g_pHyprRenderer->renderSnapshot(target.window);
    g_pHyprOpenGL->restoreMatrix();

    if (overlayAlpha < 0.999) {
        draw_rect(centeredTarget,
                  bounds,
                  CHyprColor(0.02F, 0.03F, 0.05F, static_cast<float>((1.0 - overlayAlpha) * 0.20)),
                  12);
    }
    return true;
}

void draw_window_preview(const SceneTarget& target, const Box& bounds, double overlayAlpha) {
    const auto drawBox = center_scale_box(target.box, 0.97 + 0.03 * overlayAlpha);
    const auto baseFill = target.selected ? CHyprColor(0.17F, 0.24F, 0.31F, static_cast<float>(0.30 * overlayAlpha))
                                          : CHyprColor(0.06F, 0.08F, 0.11F, static_cast<float>(0.22 * overlayAlpha));
    const auto border = target.selected ? CHyprColor(0.64F, 0.86F, 0.98F, static_cast<float>(0.92 * overlayAlpha))
                                        : CHyprColor(0.28F, 0.31F, 0.38F, static_cast<float>(0.92 * overlayAlpha));
    draw_panel(drawBox, bounds, border, baseFill, 16, 2.0);

    const auto previewBox = inset_box(drawBox, 4.0, 4.0);
    const auto drewSnapshot = draw_window_snapshot(target, previewBox, bounds, overlayAlpha);
    if (!drewSnapshot) {
        const auto fallbackFill = target.selected ? CHyprColor(0.25F, 0.38F, 0.49F, static_cast<float>(0.86 * overlayAlpha))
                                                  : CHyprColor(0.14F, 0.16F, 0.21F, static_cast<float>(0.80 * overlayAlpha));
        draw_rect(previewBox, bounds, fallbackFill, 12);
    }

    const auto titleBackdrop = Box{
        previewBox.x + 8.0,
        previewBox.y + 8.0,
        std::max(24.0, std::min(previewBox.w - 16.0, std::max(80.0, previewBox.w * 0.72))),
        std::min(28.0, std::max(20.0, previewBox.h * 0.16)),
    };
    if (titleBackdrop.w > 8.0 && titleBackdrop.h > 8.0)
        draw_rect(titleBackdrop,
                  bounds,
                  CHyprColor(0.03F, 0.04F, 0.06F, static_cast<float>(0.72 * overlayAlpha)),
                  9);
    draw_text(target.label,
              {titleBackdrop.x + 8.0, titleBackdrop.y + 3.0, std::max(24.0, titleBackdrop.w - 16.0), std::max(14.0, titleBackdrop.h - 6.0)},
              bounds,
              CHyprColor(0.95F, 0.97F, 1.0F, static_cast<float>(overlayAlpha)),
              15,
              500);
}

void draw_empty_target(const SceneTarget& target, const Box& bounds, double overlayAlpha) {
    const auto drawBox = center_scale_box(target.box, 0.97 + 0.03 * overlayAlpha);
    const auto fill = target.synthetic ? CHyprColor(0.11F, 0.29F, 0.38F, static_cast<float>(0.58 * overlayAlpha))
                                       : CHyprColor(0.12F, 0.14F, 0.18F, static_cast<float>(0.58 * overlayAlpha));
    const auto border = target.selected ? CHyprColor(0.64F, 0.86F, 0.98F, static_cast<float>(0.92 * overlayAlpha))
                                        : CHyprColor(0.28F, 0.31F, 0.38F, static_cast<float>(0.86 * overlayAlpha));
    draw_panel(drawBox, bounds, border, fill, 20, 2.0);
    draw_text(target.label, inset_box(drawBox, 12.0, 10.0), bounds, CHyprColor(0.88F, 0.92F, 0.98F, static_cast<float>(overlayAlpha)), 16, 500);
}

double overlay_progress(int monitorId, steady_tp now) {
    if (session().active()) {
        const auto it = g_openedAt.find(monitorId);
        if (it == g_openedAt.end())
            return 1.0;

        const auto elapsed = std::chrono::duration<double>(now - it->second).count();
        return ease_out_cubic(elapsed / std::chrono::duration<double>(kOpenDuration).count());
    }

    const auto it = g_closingScenes.find(monitorId);
    if (it == g_closingScenes.end())
        return 0.0;

    const auto elapsed = std::chrono::duration<double>(now - it->second.startedAt).count();
    return 1.0 - ease_out_cubic(elapsed / std::chrono::duration<double>(kCloseDuration).count());
}

bool closing_animation_finished(const ClosingScene& scene, steady_tp now) {
    return now - scene.startedAt >= kCloseDuration;
}

bool selection_animation_active(int monitorId, steady_tp now) {
    const auto it = g_selectionPulses.find(monitorId);
    return it != g_selectionPulses.end() && now - it->second.startedAt < kSelectionDuration;
}

Box animated_selection_box(const Box& selectionBox, int monitorId, steady_tp now) {
    const auto it = g_selectionPulses.find(monitorId);
    if (it == g_selectionPulses.end())
        return selectionBox;

    const auto elapsed = std::chrono::duration<double>(now - it->second.startedAt).count();
    const auto t = clamp01(elapsed / std::chrono::duration<double>(kSelectionDuration).count());
    const auto pulse = 1.0 + 0.05 * (1.0 - ease_out_cubic(t));
    return center_scale_box(selectionBox, pulse);
}

void snapshot_window_targets() {
    for (const auto& region : session().model().monitors()) {
        for (const auto& workspace : region.workspaces) {
            for (const auto& target : workspace.targets) {
                if (target.type != TargetType::Window || !target.window)
                    continue;

                g_pHyprRenderer->makeSnapshot(target.window);
            }
        }
    }
}

void snapshot_background_layers() {
    g_backgroundLayers.clear();

    for (const auto& region : session().model().monitors()) {
        auto monitor = region.monitor;
        if (!monitor)
            continue;

        auto& layers = g_backgroundLayers[monitor->m_id];
        const auto snapshotLayerList = [&layers](const auto& layerList) {
            for (const auto& layerRef : layerList) {
                auto layer = layerRef.lock();
                if (!layer || !layer->aliveAndVisible())
                    continue;

                g_pHyprRenderer->makeSnapshot(layer);
                layers.push_back(layer);
            }
        };

        // Wallpaper clients may sit on either background or bottom. Capture
        // both so overview can replay the original wallpaper layer stack.
        snapshotLayerList(monitor->m_layerSurfaceLayers[ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND]);
        snapshotLayerList(monitor->m_layerSurfaceLayers[ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM]);
    }
}

void draw_wallpaper_background(int monitorId) {
    const auto it = g_backgroundLayers.find(monitorId);
    if (it == g_backgroundLayers.end())
        return;

    for (const auto& layerRef : it->second) {
        auto layer = layerRef.lock();
        if (!layer || !layer->aliveAndVisible())
            continue;

        g_pHyprRenderer->renderSnapshot(layer);
    }
}

void draw_monitor_backdrop(int monitorId) {
    // The overview pass is injected after normal window rendering, so we need
    // to restore the compositor's monitor background first or empty regions
    // will keep showing the live desktop below the overlay.
    g_pHyprOpenGL->clearWithTex();
    draw_wallpaper_background(monitorId);
}

void draw_scene_monitor(const SceneMonitor& scene, steady_tp now) {
    const auto progress = overlay_progress(scene.monitorId, now);
    if (progress <= 0.0)
        return;

    const auto overlayBox = center_scale_box(scene.box, 0.965 + 0.035 * progress);
    const auto overlayAlpha = progress;

    draw_monitor_backdrop(scene.monitorId);

    const auto monitorChip = Box{
        16.0,
        12.0,
        std::min(std::max(112.0, scene.box.w * 0.18), std::max(112.0, scene.box.w - 32.0)),
        28.0,
    };
    draw_rect(monitorChip,
              scene.box,
              CHyprColor(0.04F, 0.05F, 0.08F, static_cast<float>(0.46 * overlayAlpha)),
              12);
    draw_text(scene.monitorName.empty() ? "monitor" : scene.monitorName,
              {monitorChip.x + 10.0, monitorChip.y + 3.0, std::max(56.0, monitorChip.w - 20.0), monitorChip.h - 6.0},
              scene.box,
              CHyprColor(0.84F, 0.88F, 0.94F, static_cast<float>(overlayAlpha)),
              16,
              500);

    for (const auto& workspace : scene.workspaces) {
        const auto workspaceBox = center_scale_box(workspace.box, 0.97 + 0.03 * progress);
        const auto border = workspace.selected ? CHyprColor(0.53F, 0.74F, 0.88F, static_cast<float>(0.92 * overlayAlpha))
                                               : CHyprColor(0.20F, 0.23F, 0.28F, static_cast<float>(0.92 * overlayAlpha));
        const auto fill = workspace.special ? CHyprColor(0.08F, 0.12F, 0.18F, static_cast<float>(0.20 * overlayAlpha))
                                            : CHyprColor(0.04F, 0.05F, 0.08F, static_cast<float>(0.14 * overlayAlpha));
        draw_panel(workspaceBox, scene.box, border, fill, 24, 2.0);

        const auto workspaceChip = Box{
            workspaceBox.x + 10.0,
            workspaceBox.y + 8.0,
            std::min(std::max(72.0, workspaceBox.w * 0.28), std::max(72.0, workspaceBox.w - 20.0)),
            22.0,
        };
        draw_rect(workspaceChip,
                  scene.box,
                  CHyprColor(0.05F, 0.06F, 0.09F, static_cast<float>(0.58 * overlayAlpha)),
                  9);
        draw_text(workspace.label,
                  {workspaceChip.x + 8.0, workspaceChip.y + 2.0, std::max(40.0, workspaceChip.w - 16.0), workspaceChip.h - 4.0},
                  scene.box,
                  CHyprColor(0.92F, 0.95F, 1.0F, static_cast<float>(overlayAlpha)),
                  16,
                  600);

        for (const auto& target : workspace.targets) {
            if (target.type == TargetType::Window)
                draw_window_preview(target, scene.box, overlayAlpha);
            else
                draw_empty_target(target, scene.box, overlayAlpha);
        }
    }

    if (scene.workspaces.empty()) {
        draw_text("No tiled workspaces",
                  {overlayBox.x + std::max(24.0, overlayBox.w * 0.18), overlayBox.y + overlayBox.h * 0.5 - 12.0,
                   std::max(120.0, overlayBox.w * 0.64), 24.0},
                  scene.box,
                  CHyprColor(0.76F, 0.80F, 0.86F, static_cast<float>(overlayAlpha)),
                  18,
                  500);
    }

    if (scene.syntheticTarget)
        draw_empty_target(*scene.syntheticTarget, scene.box, overlayAlpha);

    if (scene.selectionBox) {
        const auto selectionBox = animated_selection_box(*scene.selectionBox, scene.monitorId, now);
        draw_panel(center_scale_box(selectionBox, 1.02),
                   scene.box,
                   CHyprColor(0.91F, 0.76F, 0.27F, static_cast<float>(0.96 * overlayAlpha)),
                   CHyprColor(0.91F, 0.76F, 0.27F, static_cast<float>(0.12 * overlayAlpha)),
                   18,
                   2.0);
    }
}

void update_selection_pulse(const SceneMonitor& scene, steady_tp now) {
    if (!scene.selectionBox) {
        g_selectionPulses.erase(scene.monitorId);
        return;
    }

    const auto it = g_selectionPulses.find(scene.monitorId);
    if (it == g_selectionPulses.end() || !boxes_match(it->second.box, *scene.selectionBox)) {
        g_selectionPulses[scene.monitorId] = SelectionPulse{
            .box = *scene.selectionBox,
            .startedAt = now,
        };
    }
}

void damage_monitor_if_animating(PHLMONITOR monitor, steady_tp now) {
    if (!monitor)
        return;

    const auto openIt = g_openedAt.find(monitor->m_id);
    if (session().active() && openIt != g_openedAt.end() && now - openIt->second < kOpenDuration) {
        g_pHyprRenderer->damageMonitor(monitor);
        return;
    }

    if (selection_animation_active(monitor->m_id, now)) {
        g_pHyprRenderer->damageMonitor(monitor);
        return;
    }

    const auto closeIt = g_closingScenes.find(monitor->m_id);
    if (closeIt != g_closingScenes.end() && !closing_animation_finished(closeIt->second, now))
        g_pHyprRenderer->damageMonitor(monitor);
}

void handle_session_transition(steady_tp now) {
    const auto overviewActive = session().active();
    if (overviewActive == g_lastOverviewActive)
        return;

    if (overviewActive) {
        g_closingScenes.clear();
        g_openedAt.clear();
        g_backgroundLayers.clear();
        g_selectionPulses.clear();
        snapshot_window_targets();
        snapshot_background_layers();
        for (const auto& region : session().model().monitors()) {
            g_openedAt[region.monitorId] = now;
            if (region.monitor)
                g_pHyprRenderer->damageMonitor(region.monitor);
        }
        spdlog::info("overview_renderer: enabled monitors={}", session().model().monitors().size());
    } else {
        g_openedAt.clear();
        g_selectionPulses.clear();
        for (const auto& [monitorId, scene] : g_liveScenes) {
            g_closingScenes[monitorId] = ClosingScene{
                .scene = scene,
                .startedAt = now,
            };
            if (const auto monitor = g_pCompositor->getMonitorFromID(monitorId))
                g_pHyprRenderer->damageMonitor(monitor);
        }
        spdlog::info("overview_renderer: disabled monitors={}", g_closingScenes.size());
    }

    g_lastOverviewActive = overviewActive;
}

void update_live_scene_for_monitor(PHLMONITOR monitor, steady_tp now) {
    const auto scene = buildSceneForMonitor(monitor, session().model());
    if (!scene) {
        g_liveScenes.erase(monitor->m_id);
        return;
    }

    update_selection_pulse(*scene, now);
    g_liveScenes[monitor->m_id] = *scene;
    g_closingScenes.erase(monitor->m_id);
}

} // namespace

void fullRenderMonitor(PHLMONITOR monitor) {
    if (!monitor)
        return;

    const auto now = std::chrono::steady_clock::now();

    if (session().active()) {
        const auto it = g_liveScenes.find(monitor->m_id);
        if (it == g_liveScenes.end())
            return;

        draw_scene_monitor(it->second, now);
        return;
    }

    const auto it = g_closingScenes.find(monitor->m_id);
    if (it == g_closingScenes.end())
        return;

    draw_scene_monitor(it->second.scene, now);
}

bool initializeRendererHooks(HANDLE handle) {
    (void)handle;
    if (g_renderPreListener || g_renderStageListener || g_keyboardKeyListener)
        return true;

    try {
        g_renderPreListener = Event::bus()->m_events.render.pre.listen([](PHLMONITOR monitor) {
            if (!monitor)
                return;

            const auto now = std::chrono::steady_clock::now();
            handle_session_transition(now);

            if (session().active()) {
                update_live_scene_for_monitor(monitor, now);
            } else {
                auto it = g_closingScenes.find(monitor->m_id);
                if (it != g_closingScenes.end() && closing_animation_finished(it->second, now))
                    g_closingScenes.erase(it);
            }

            damage_monitor_if_animating(monitor, now);
        });

        g_renderStageListener = Event::bus()->m_events.render.stage.listen([](eRenderStage stage) {
            if (stage != RENDER_POST_WINDOWS)
                return;

            const auto monitor = g_pHyprOpenGL->m_renderData.pMonitor.lock();
            if (!monitor)
                return;

            if (session().active()) {
                if (g_liveScenes.contains(monitor->m_id))
                    g_pHyprRenderer->m_renderPass.add(makeUnique<OverviewPassElement>(monitor));
                return;
            }

            const auto it = g_closingScenes.find(monitor->m_id);
            if (it != g_closingScenes.end() && !closing_animation_finished(it->second, std::chrono::steady_clock::now()))
                g_pHyprRenderer->m_renderPass.add(makeUnique<OverviewPassElement>(monitor));
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
        g_openedAt.clear();
        g_closingScenes.clear();
        g_liveScenes.clear();
        g_backgroundLayers.clear();
        g_selectionPulses.clear();
        g_textCache.clear();
        g_lastOverviewActive = false;
        spdlog::warn("overview_renderer_init: disabled after exception: {}", e.what());
        return false;
    }
}

void shutdownRendererHooks(HANDLE handle) {
    (void)handle;
    g_renderPreListener = nullptr;
    g_renderStageListener = nullptr;
    g_keyboardKeyListener = nullptr;
    g_openedAt.clear();
    g_closingScenes.clear();
    g_liveScenes.clear();
    g_backgroundLayers.clear();
    g_selectionPulses.clear();
    g_textCache.clear();
    g_lastOverviewActive = false;
    g_pHyprRenderer->m_renderPass.removeAllOfType("OverviewPassElement");
    spdlog::info("overview_renderer_shutdown");
}

} // namespace Overview
