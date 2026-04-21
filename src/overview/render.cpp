/**
 * @file render.cpp
 * @brief Stable overview renderer built on top of Hyprland render passes.
 *
 * The overview renderer intentionally avoids symbol scanning and any mutation
 * of real window geometry. It draws a monitor-local overlay from the read-only
 * overview session model without turning overview into an editing mode.
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
#include <hyprland/src/config/ConfigDataValues.hpp>
#include <hyprland/src/desktop/view/LayerSurface.hpp>
#include <hyprland/src/desktop/Workspace.hpp>
#include <hyprland/src/event/EventBus.hpp>
#include <hyprland/src/helpers/Color.hpp>
#include <hyprland/src/plugins/PluginAPI.hpp>
#include <hyprland/src/protocols/LayerShell.hpp>
#include <hyprland/src/render/OpenGL.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprland/src/render/pass/ClearPassElement.hpp>
#include <spdlog/spdlog.h>

#include "pass_element.h"
#include "scene.h"
#include "session.h"

namespace Overview {
namespace {

using ScrollerCore::Box;
using steady_tp = std::chrono::steady_clock::time_point;

constexpr auto kOpenDuration = std::chrono::milliseconds(180);
constexpr auto kSelectionDuration = std::chrono::milliseconds(120);

struct SelectionPulse {
    Box      box;
    steady_tp startedAt;
};

CHyprSignalListener                         g_renderPreListener = nullptr;
CHyprSignalListener                         g_renderStageListener = nullptr;
CHyprSignalListener                         g_keyboardKeyListener = nullptr;
std::unordered_map<int, steady_tp>         g_openedAt;
std::unordered_map<int, SceneMonitor>      g_liveScenes;
std::unordered_map<int, std::vector<PHLLSREF>> g_backdropLayers;
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

void draw_outline_panel(const Box& box, const Box& bounds, const CHyprColor& color, int round, float roundingPower = 2.0F, double borderWidth = 2.0) {
    const auto clipped = intersect_box(box, bounds);
    if (!clipped || clipped->w <= 2.0 || clipped->h <= 2.0 || borderWidth <= 0.0)
        return;

    CGradientValueData gradient(color);
    CHyprOpenGLImpl::SBorderRenderData data;
    data.round = std::clamp(round, 0, iround(std::min(clipped->w, clipped->h) * 0.5));
    data.outerRound = data.round;
    data.roundingPower = roundingPower;
    data.borderSize = std::max(1, iround(borderWidth));
    data.a = 1.0F;
    g_pHyprOpenGL->renderBorder(to_cbox(*clipped), gradient, data);
}

struct PreviewShape {
    int   round = 12;
    float roundingPower = 2.0F;
};

PreviewShape preview_shape_for_window(const SceneTarget& target, const Box& box) {
    PreviewShape shape;
    if (!target.window)
        return shape;

    const auto sourceBox = target.window->getWindowMainSurfaceBox();
    if (sourceBox.w <= 1.0 || sourceBox.h <= 1.0)
        return shape;

    const auto scale = std::min(box.w / std::max(1.0, sourceBox.w),
                                box.h / std::max(1.0, sourceBox.h));
    const auto maxRound = std::max(4, iround(std::min(box.w, box.h) * 0.5));
    shape.round = std::clamp(iround(target.window->rounding() * scale), 4, maxRound);
    shape.roundingPower = target.window->roundingPower();
    return shape;
}

bool draw_window_snapshot(const SceneTarget& target, const Box& box, const Box& bounds) {
    if (!target.window)
        return false;

    const auto clipped = intersect_box(box, bounds);
    if (!clipped || clipped->w <= 4.0 || clipped->h <= 4.0)
        return false;

    const auto monitor = g_pHyprOpenGL->m_renderData.pMonitor.lock();
    const auto sourceMonitor = target.window->m_monitor.lock();
    if (!monitor || !sourceMonitor || sourceMonitor->m_id != monitor->m_id)
        return false;

    PHLWINDOWREF ref{target.window};
    const auto framebufferIt = g_pHyprOpenGL->m_windowFramebuffers.find(ref);
    if (framebufferIt == g_pHyprOpenGL->m_windowFramebuffers.end())
        return false;

    const auto texture = framebufferIt->second.getTexture();
    if (!texture)
        return false;

    const auto sourceBox = CBox{
        target.window->m_position.x - sourceMonitor->m_position.x,
        target.window->m_position.y - sourceMonitor->m_position.y,
        target.window->m_size.x,
        target.window->m_size.y,
    };
    if (sourceBox.width <= 1.0 || sourceBox.height <= 1.0)
        return false;

    const auto scale = std::min(clipped->w / std::max(1.0, sourceBox.width),
                                clipped->h / std::max(1.0, sourceBox.height));
    if (!std::isfinite(scale) || scale <= 0.0)
        return false;

    const auto centeredTarget = CBox{
        clipped->x + (clipped->w - sourceBox.width * scale) * 0.5,
        clipped->y + (clipped->h - sourceBox.height * scale) * 0.5,
        sourceBox.width * scale,
        sourceBox.height * scale,
    };

    auto uvBox = CBox{
        sourceBox.x / std::max(1.0, sourceMonitor->m_size.x),
        sourceBox.y / std::max(1.0, sourceMonitor->m_size.y),
        sourceBox.width / std::max(1.0, sourceMonitor->m_size.x),
        sourceBox.height / std::max(1.0, sourceMonitor->m_size.y),
    };
    uvBox.transform(Math::wlTransformToHyprutils(Math::invertTransform(sourceMonitor->m_transform)), 1.0, 1.0);

    const auto lastUVTL = g_pHyprOpenGL->m_renderData.primarySurfaceUVTopLeft;
    const auto lastUVBR = g_pHyprOpenGL->m_renderData.primarySurfaceUVBottomRight;
    const auto lastWindow = g_pHyprOpenGL->m_renderData.currentWindow;
    const auto lastTransform = texture->m_transform;
    auto uvTopLeft = Vector2D(uvBox.x, uvBox.y);
    auto uvBottomRight = Vector2D(uvBox.x + uvBox.width, uvBox.y + uvBox.height);
    if (sourceMonitor->m_transform % 2 == 1) {
        std::swap(uvTopLeft.x, uvBottomRight.x);
        std::swap(uvTopLeft.y, uvBottomRight.y);
    }
    g_pHyprOpenGL->m_renderData.primarySurfaceUVTopLeft = uvTopLeft;
    g_pHyprOpenGL->m_renderData.primarySurfaceUVBottomRight = uvBottomRight;
    g_pHyprOpenGL->m_renderData.currentWindow = nullptr;
    texture->m_transform = Math::wlTransformToHyprutils(sourceMonitor->m_transform);

    const auto shape = preview_shape_for_window(target, {centeredTarget.x, centeredTarget.y, centeredTarget.width, centeredTarget.height});
    CHyprOpenGLImpl::STextureRenderData data;
    data.a = 1.0F;
    data.round = shape.round;
    data.roundingPower = shape.roundingPower;
    data.allowCustomUV = true;
    data.blockBlurOptimization = true;
    g_pHyprOpenGL->renderTexture(texture, centeredTarget, data);

    g_pHyprOpenGL->m_renderData.primarySurfaceUVTopLeft = lastUVTL;
    g_pHyprOpenGL->m_renderData.primarySurfaceUVBottomRight = lastUVBR;
    g_pHyprOpenGL->m_renderData.currentWindow = lastWindow;
    texture->m_transform = lastTransform;
    return true;
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

void draw_window_preview(const SceneTarget& target, const Box& bounds, double overlayAlpha) {
    const auto drawBox = center_scale_box(target.box, 0.97 + 0.03 * overlayAlpha);
    const auto previewBox = inset_box(drawBox, 2.0, 2.0);
    const auto previewShape = preview_shape_for_window(target, previewBox);
    const auto border = target.selected ? CHyprColor(0.64F, 0.86F, 0.98F, static_cast<float>(0.92 * overlayAlpha))
                                        : CHyprColor(0.28F, 0.31F, 0.38F, static_cast<float>(0.92 * overlayAlpha));
    const auto shadowAlpha = target.selected ? 0.22F : 0.14F;
    g_pHyprOpenGL->renderRoundedShadow(to_cbox(previewBox), previewShape.round, previewShape.roundingPower, 18,
                                       CHyprColor(0.00F, 0.00F, 0.00F, shadowAlpha * overlayAlpha), 1.0F);

    if (!draw_window_snapshot(target, previewBox, bounds)) {
        draw_rect(previewBox,
                  bounds,
                  target.selected ? CHyprColor(0.16F, 0.22F, 0.29F, static_cast<float>(0.82 * overlayAlpha))
                                  : CHyprColor(0.08F, 0.10F, 0.14F, static_cast<float>(0.74 * overlayAlpha)),
                  previewShape.round,
                  previewShape.roundingPower);
    }

    draw_outline_panel(drawBox, bounds, border, previewShape.round + 2, previewShape.roundingPower, 2.0);

    const auto titleBackdrop = Box{
        previewBox.x + 10.0,
        previewBox.y + 10.0,
        std::max(24.0, std::min(previewBox.w - 20.0, std::max(80.0, previewBox.w * 0.72))),
        std::min(28.0, std::max(20.0, previewBox.h * 0.12)),
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
    const auto border = target.selected ? CHyprColor(0.64F, 0.86F, 0.98F, static_cast<float>(0.92 * overlayAlpha))
                                        : CHyprColor(0.28F, 0.31F, 0.38F, static_cast<float>(0.86 * overlayAlpha));
    draw_outline_panel(drawBox, bounds, border, 20, 2.0F, 2.0);
}

double overlay_progress(int monitorId, steady_tp now) {
    if (session().active()) {
        const auto it = g_openedAt.find(monitorId);
        if (it == g_openedAt.end())
            return 1.0;

        const auto elapsed = std::chrono::duration<double>(now - it->second).count();
        return ease_out_cubic(elapsed / std::chrono::duration<double>(kOpenDuration).count());
    }

    return 0.0;
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

void snapshot_backdrop_layers() {
    g_backdropLayers.clear();

    for (const auto& region : session().model().monitors()) {
        auto monitor = region.monitor;
        if (!monitor)
            continue;

        auto& layers = g_backdropLayers[monitor->m_id];
        const auto snapshotLayerList = [&layers](const auto& layerList) {
            for (const auto& layerRef : layerList) {
                auto layer = layerRef.lock();
                if (!layer || !layer->aliveAndVisible())
                    continue;

                g_pHyprRenderer->makeSnapshot(layer);
                layers.push_back(layer);
            }
        };

        // Wallpaper clients are usually on background or bottom. Capture both
        // and preserve Hyprland's normal render order.
        snapshotLayerList(monitor->m_layerSurfaceLayers[ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND]);
        snapshotLayerList(monitor->m_layerSurfaceLayers[ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM]);
    }
}

void enqueue_monitor_backdrop(PHLMONITOR monitor) {
    if (!monitor)
        return;

    // These pass elements must be queued while Hyprland is still building the
    // render pass. Calling renderSnapshot from inside our own pass element
    // draw() mutates the pass during iteration, which makes backdrop ordering
    // unreliable.
    //
    // Avoid clearWithTex(): that reuses Hyprland's monitor background texture,
    // which is not the original wallpaper layer stack and can reintroduce blur
    // or monitor-local transition flicker. Use a stable matte under the
    // captured background/bottom layers instead.
    g_pHyprRenderer->m_renderPass.add(makeUnique<CClearPassElement>(CClearPassElement::SClearData{
        CHyprColor(0.02F, 0.03F, 0.05F, 1.0F),
    }));

    const auto it = g_backdropLayers.find(monitor->m_id);
    if (it == g_backdropLayers.end())
        return;

    for (const auto& layerRef : it->second) {
        auto layer = layerRef.lock();
        if (!layer || !layer->aliveAndVisible())
            continue;

        g_pHyprRenderer->renderSnapshot(layer);
    }
}

void draw_scene_monitor(const SceneMonitor& scene, steady_tp now) {
    const auto progress = overlay_progress(scene.monitorId, now);
    if (progress <= 0.0)
        return;

    const auto overlayAlpha = progress;

    for (const auto& workspace : scene.workspaces) {
        for (const auto& target : workspace.targets) {
            if (target.type == TargetType::Window)
                draw_window_preview(target, scene.box, overlayAlpha);
            else
                draw_empty_target(target, scene.box, overlayAlpha);
        }
    }

    if (scene.syntheticTarget)
        draw_empty_target(*scene.syntheticTarget, scene.box, overlayAlpha);

    if (scene.selectionBox) {
        const auto selectionBox = animated_selection_box(*scene.selectionBox, scene.monitorId, now);
        draw_outline_panel(center_scale_box(selectionBox, 1.02),
                           scene.box,
                           CHyprColor(0.91F, 0.76F, 0.27F, static_cast<float>(0.96 * overlayAlpha)),
                           18,
                           2.0F,
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
}

void handle_session_transition(steady_tp now) {
    const auto overviewActive = session().active();
    if (overviewActive == g_lastOverviewActive)
        return;

    if (overviewActive) {
        g_openedAt.clear();
        g_backdropLayers.clear();
        g_selectionPulses.clear();
        snapshot_window_targets();
        snapshot_backdrop_layers();
        for (const auto& region : session().model().monitors()) {
            g_openedAt[region.monitorId] = now;
            if (region.monitor)
                g_pHyprRenderer->damageMonitor(region.monitor);
        }
        spdlog::info("overview_renderer: enabled monitors={}", session().model().monitors().size());
    } else {
        g_openedAt.clear();
        g_selectionPulses.clear();
        for (const auto& [monitorId, _scene] : g_liveScenes) {
            if (const auto monitor = g_pCompositor->getMonitorFromID(monitorId))
                g_pHyprRenderer->damageMonitor(monitor);
        }
        g_liveScenes.clear();
        spdlog::info("overview_renderer: disabled monitors=0");
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
    }
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

            if (session().active())
                update_live_scene_for_monitor(monitor, now);

            damage_monitor_if_animating(monitor, now);
        });

        g_renderStageListener = Event::bus()->m_events.render.stage.listen([](eRenderStage stage) {
            if (stage != RENDER_POST_WINDOWS)
                return;

            const auto monitor = g_pHyprOpenGL->m_renderData.pMonitor.lock();
            if (!monitor)
                return;

            if (session().active()) {
                if (g_liveScenes.contains(monitor->m_id)) {
                    enqueue_monitor_backdrop(monitor);
                    g_pHyprRenderer->m_renderPass.add(makeUnique<OverviewPassElement>(monitor));
                }
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
        g_openedAt.clear();
        g_liveScenes.clear();
        g_backdropLayers.clear();
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
    g_liveScenes.clear();
    g_backdropLayers.clear();
    g_selectionPulses.clear();
    g_textCache.clear();
    g_lastOverviewActive = false;
    g_pHyprRenderer->m_renderPass.removeAllOfType("OverviewPassElement");
    spdlog::info("overview_renderer_shutdown");
}

} // namespace Overview
