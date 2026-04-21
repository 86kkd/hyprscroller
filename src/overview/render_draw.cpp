/**
 * @file render_draw.cpp
 * @brief Rendering helpers for overview scene drawing and snapshot setup.
 *
 * This file is the last step of the overview pipeline:
 * - `session.cpp` decides what should be selected
 * - `scene.cpp` turns the model into render DTOs
 * - this file turns those DTOs into OpenGL draw calls and snapshot setup
 *
 * New-reader rule of thumb:
 * - helpers in the anonymous namespace are low-level drawing primitives
 * - exported functions at the bottom are the render pipeline entrypoints
 * - whenever OpenGL render state is temporarily changed, restore it before exit
 */
#include "render_draw.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/desktop/view/LayerSurface.hpp>
#include <hyprland/src/protocols/LayerShell.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprland/src/render/pass/ClearPassElement.hpp>

#include "geometry_utils.h"
#include "style.h"

namespace Overview {
namespace {

using ScrollerCore::Box;

// Render APIs expect integer-ish boxes; overview layout math works in doubles.
int iround(double value) {
    return static_cast<int>(std::lround(value));
}

CBox to_cbox(const Box& box) {
    return CBox{
        static_cast<double>(iround(box.x)),
        static_cast<double>(iround(box.y)),
        static_cast<double>(std::max(1, iround(box.w))),
        static_cast<double>(std::max(1, iround(box.h))),
    };
}

// Text rendering is expensive enough that overview keeps a small cache keyed by
// text + style. The cache lives in `RenderState`; these helpers only compute the
// lookup key and populate the cache on misses.
std::string text_cache_key(const std::string& text, const CHyprColor& color, int pt, int maxWidth, int weight) {
    return text + "|" + std::to_string(color.stripA().getAsHex()) + "|" + std::to_string(pt) + "|" + std::to_string(maxWidth) + "|" + std::to_string(weight);
}

SP<CTexture> get_text_texture(RenderState& state, const std::string& text, const CHyprColor& color, int pt, int maxWidth = 0, int weight = 400) {
    if (text.empty())
        return nullptr;

    const auto key = text_cache_key(text, color, pt, maxWidth, weight);
    if (const auto texture = state.findTextTexture(key))
        return texture;

    auto texture = g_pHyprOpenGL->renderText(text, color.stripA(), pt, false, "", maxWidth, weight);
    state.storeTextTexture(key, texture);
    return texture;
}

void draw_text(RenderState& state, const std::string& text, const Box& box, const Box& bounds, const CHyprColor& color, int pt, int weight = 400) {
    const auto clipped = intersectBox(box, bounds);
    if (!clipped || clipped->w <= 4.0 || clipped->h <= 4.0)
        return;

    auto texture = get_text_texture(state, text, color, pt, std::max(1, iround(clipped->w)), weight);
    if (!texture || texture->m_size.x <= 0.0 || texture->m_size.y <= 0.0)
        return;

    // Scale text to fit the requested title box while preserving aspect ratio.
    const auto scale = std::min(clipped->w / texture->m_size.x, clipped->h / texture->m_size.y);
    const auto width = std::max(1.0, texture->m_size.x * scale);
    const auto height = std::max(1.0, texture->m_size.y * scale);
    const Box drawBox{
        clipped->x,
        clipped->y + std::max(0.0, (clipped->h - height) * 0.5),
        width,
        height,
    };
    const auto finalBox = intersectBox(drawBox, bounds);
    if (!finalBox)
        return;

    CHyprOpenGLImpl::STextureRenderData data;
    data.a = static_cast<float>(color.a);
    data.blockBlurOptimization = true;
    g_pHyprOpenGL->renderTexture(texture, to_cbox(*finalBox), data);
}

void draw_rect(const Box& box, const Box& bounds, const CHyprColor& color, int round, float roundingPower = 2.0F) {
    const auto clipped = intersectBox(box, bounds);
    if (!clipped)
        return;

    CHyprOpenGLImpl::SRectRenderData data;
    data.round = round;
    data.roundingPower = roundingPower;
    g_pHyprOpenGL->renderRect(to_cbox(*clipped), color, data);
}

void draw_outline_panel(const Box& box, const Box& bounds, const CHyprColor& color, int round, float roundingPower = 2.0F, double borderWidth = Style::kOutlineBorderWidth) {
    const auto clipped = intersectBox(box, bounds);
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
    int   round = Style::kDefaultRound;
    float roundingPower = 2.0F;
};

// Snapshot previews try to mimic the live window's rounded-corner style after
// the preview has been scaled down into overview space.
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

// Draw one window preview directly from Hyprland's window framebuffer snapshot.
// This path temporarily overrides a few OpenGL render-state fields so the
// preview samples the correct UV rectangle from the source monitor texture.
bool draw_window_snapshot(const SceneTarget& target, const Box& box, const Box& bounds) {
    if (!target.window)
        return false;

    const auto clipped = intersectBox(box, bounds);
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

    // Save every mutable render-state field we touch so overview drawing leaves
    // the surrounding render pass exactly as it found it.
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

void draw_window_preview(RenderState& state, const SceneTarget& target, const Box& bounds, double overlayAlpha) {
    // The preview draw order is:
    // 1. shadow
    // 2. live snapshot or matte fallback
    // 3. outline
    // 4. title backdrop + text
    const auto drawBox = centerScaleBox(target.box, Style::kTargetScaleBase + Style::kTargetScaleRange * overlayAlpha);
    const auto previewBox = insetBox(drawBox, Style::kPreviewInset, Style::kPreviewInset);
    const auto previewShape = preview_shape_for_window(target, previewBox);
    const auto border = Style::previewBorder(target.selected, static_cast<float>(0.92 * overlayAlpha));
    const auto shadowAlpha = target.selected ? 0.22F : 0.14F;
    g_pHyprOpenGL->renderRoundedShadow(to_cbox(previewBox), previewShape.round, previewShape.roundingPower, 18,
                                       Style::previewShadow(shadowAlpha * overlayAlpha), 1.0F);

    if (!draw_window_snapshot(target, previewBox, bounds)) {
        draw_rect(previewBox,
                  bounds,
                  Style::previewFallbackFill(target.selected, static_cast<float>((target.selected ? 0.82 : 0.74) * overlayAlpha)),
                  previewShape.round,
                  previewShape.roundingPower);
    }

    draw_outline_panel(drawBox, bounds, border, previewShape.round + 2, previewShape.roundingPower);

    const auto titleBackdrop = Box{
        previewBox.x + Style::kTitleBackdropInset,
        previewBox.y + Style::kTitleBackdropInset,
        std::max(24.0, std::min(previewBox.w - Style::kTitleBackdropInset * 2.0,
                                std::max(Style::kTitleBackdropMinWidth, previewBox.w * Style::kTitleBackdropMaxWidthRatio))),
        std::min(Style::kTitleBackdropMaxHeight, std::max(Style::kTitleBackdropMinHeight, previewBox.h * Style::kTitleBackdropHeightRatio)),
    };
    if (titleBackdrop.w > 8.0 && titleBackdrop.h > 8.0)
        draw_rect(titleBackdrop,
                  bounds,
                  Style::titleBackdrop(static_cast<float>(0.72 * overlayAlpha)),
                  Style::kTitleBackdropRound);
    draw_text(state,
              target.label,
              {titleBackdrop.x + Style::kTitleTextInsetX,
               titleBackdrop.y + Style::kTitleTextInsetY,
               std::max(24.0, titleBackdrop.w - Style::kTitleTextInsetX * 2.0),
               std::max(14.0, titleBackdrop.h - Style::kTitleTextInsetY * 2.0)},
              bounds,
              Style::titleText(static_cast<float>(overlayAlpha)),
              15,
              500);
}

void draw_empty_target(const SceneTarget& target, const Box& bounds, double overlayAlpha) {
    // Empty targets are intentionally minimal: they only need an outline to
    // show "there is navigable blank space here".
    const auto drawBox = centerScaleBox(target.box, Style::kTargetScaleBase + Style::kTargetScaleRange * overlayAlpha);
    const auto border = Style::previewBorder(target.selected, static_cast<float>((target.selected ? 0.92 : 0.86) * overlayAlpha));
    draw_outline_panel(drawBox, bounds, border, Style::kEmptyTargetRound, 2.0F);
}

} // namespace

void snapshotWindowTargets(const Model& model) {
    // Window previews prefer cached snapshots over re-rendering live window
    // trees during overview draw. Snapshot the participating windows up front at
    // session transition time so later draw passes are simple and stable.
    for (const auto& region : model.monitors()) {
        for (const auto& workspace : region.workspaces) {
            for (const auto& target : workspace.targets) {
                if (target.type != TargetType::Window || !target.window)
                    continue;

                g_pHyprRenderer->makeSnapshot(target.window);
            }
        }
    }
}

void snapshotBackdropLayers(const Model& model, RenderState& state) {
    // The backdrop is captured separately from window targets so overview can
    // recreate "wallpaper beneath previews" without mutating the normal window
    // render order.
    state.clearBackdropLayers();

    for (const auto& region : model.monitors()) {
        auto monitor = region.monitor;
        if (!monitor)
            continue;

        const auto snapshotLayerList = [&](const auto& layerList) {
            for (const auto& layerRef : layerList) {
                auto layer = layerRef.lock();
                if (!layer || !layer->aliveAndVisible())
                    continue;

                g_pHyprRenderer->makeSnapshot(layer);
                state.appendBackdropLayer(monitor->m_id, layer);
            }
        };

        snapshotLayerList(monitor->m_layerSurfaceLayers[ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND]);
        snapshotLayerList(monitor->m_layerSurfaceLayers[ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM]);
    }
}

void enqueueMonitorBackdrop(PHLMONITOR monitor, const RenderState& state) {
    if (!monitor)
        return;

    // Always paint a stable matte first, then replay captured background/bottom
    // layer snapshots. That keeps overview visually stable even if the monitor
    // background texture itself is changing underneath us.
    g_pHyprRenderer->m_renderPass.add(makeUnique<CClearPassElement>(CClearPassElement::SClearData{
        Style::matteBackground(),
    }));

    const auto* layers = state.backdropLayersForMonitor(monitor->m_id);
    if (!layers)
        return;

    for (const auto& layerRef : *layers) {
        auto layer = layerRef.lock();
        if (!layer || !layer->aliveAndVisible())
            continue;

        g_pHyprRenderer->renderSnapshot(layer);
    }
}

void drawSceneMonitor(const SceneMonitor& scene, steady_tp now, RenderState& state) {
    const auto progress = state.overlayProgress(scene.monitorId, now, true);
    if (progress <= 0.0)
        return;

    // Draw in increasing specificity:
    // - workspace targets
    // - synthetic empty target, if any
    // - one monitor-local selection outline on top
    for (const auto& workspace : scene.workspaces) {
        for (const auto& target : workspace.targets) {
            if (target.type == TargetType::Window)
                draw_window_preview(state, target, scene.box, progress);
            else
                draw_empty_target(target, scene.box, progress);
        }
    }

    if (scene.syntheticTarget)
        draw_empty_target(*scene.syntheticTarget, scene.box, progress);

    if (scene.selectionBox) {
        const auto selectionBox = state.animatedSelectionBox(*scene.selectionBox, scene.monitorId, now);
        draw_outline_panel(centerScaleBox(selectionBox, Style::kSelectionOutlineScale),
                           scene.box,
                           Style::selectionOutline(static_cast<float>(0.96 * progress)),
                           18,
                           2.0F);
    }
}

} // namespace Overview
