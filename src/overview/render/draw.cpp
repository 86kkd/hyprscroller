/**
 * @file overview/render/draw.cpp
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
#include "overview/render/draw.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/config/shared/complex/ComplexDataTypes.hpp>
#include <hyprland/src/desktop/view/LayerSurface.hpp>
#include <hyprland/src/protocols/LayerShell.hpp>
#include <hyprland/src/protocols/XDGShell.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprland/src/render/pass/ClearPassElement.hpp>
#include <hyprland/src/xwayland/XSurface.hpp>

#include "overview/scene/geometry_utils.h"
#include "overview/render/style.h"

namespace Overview {
namespace {

using ScrollerCore::Box;
using Render::GL::CHyprOpenGLImpl;
using Render::GL::g_pHyprOpenGL;

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

SP<Render::ITexture> get_text_texture(RenderState& state, const std::string& text, const CHyprColor& color, int pt, int maxWidth = 0, int weight = 400) {
    if (text.empty())
        return nullptr;

    const auto key = text_cache_key(text, color, pt, maxWidth, weight);
    if (const auto texture = state.findTextTexture(key))
        return texture;

    auto texture = g_pHyprRenderer->renderText(text, color.stripA(), pt, false, "", maxWidth, weight);
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

    Config::CGradientValueData gradient(color);
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

Box scaled_target_box(const Box& box, double overlayAlpha) {
    return centerScaleBox(box, Style::kTargetScaleBase + Style::kTargetScaleRange * overlayAlpha);
}

Box animated_window_draw_box(const RenderState& state, int monitorId, const SceneTarget& target, double overlayAlpha) {
    return scaled_target_box(state.animatedPreviewBox(monitorId, target.window, target.box), overlayAlpha);
}

struct ScopedRenderDataState {
    Vector2D uvTopLeft;
    Vector2D uvBottomRight;
    PHLWINDOWREF currentWindow;

    ScopedRenderDataState()
        : uvTopLeft(g_pHyprRenderer->m_renderData.primarySurfaceUVTopLeft)
        , uvBottomRight(g_pHyprRenderer->m_renderData.primarySurfaceUVBottomRight)
        , currentWindow(g_pHyprRenderer->m_renderData.currentWindow) {
    }

    ~ScopedRenderDataState() {
        g_pHyprRenderer->m_renderData.primarySurfaceUVTopLeft = uvTopLeft;
        g_pHyprRenderer->m_renderData.primarySurfaceUVBottomRight = uvBottomRight;
        g_pHyprRenderer->m_renderData.currentWindow = currentWindow;
    }
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

// Overview previews want the window's main wl_surface even when the window owns
// subsurfaces/popups. `getSolitaryResource()` only works for single-surface
// windows, which is why multi-surface apps such as Firefox ended up blank here.
SP<CWLSurfaceResource> resolve_window_main_surface(PHLWINDOW window) {
    if (!window)
        return nullptr;

    if (const auto xdgSurface = window->m_xdgSurface.lock()) {
        if (const auto surface = xdgSurface->m_surface.lock())
            return surface;
    }

    if (const auto xwaylandSurface = window->m_xwaylandSurface.lock()) {
        if (const auto surface = xwaylandSurface->m_surface.lock())
            return surface;
    }

    return window->getSolitaryResource();
}

// For live-surface previews, use the window's real main-surface box as the
// source aspect ratio. `surfaceLogicalBox()` describes already-cropped content
// and can over-zoom xdg-shell windows when the preview is scaled down.
CBox preview_surface_source_box(PHLWINDOW window) {
    if (!window)
        return {};

    return window->getWindowMainSurfaceBox();
}

Vector2D surface_uv_top_left(SP<CWLSurfaceResource> surface) {
    if (!surface || !surface->m_current.viewport.hasSource || surface->m_current.bufferSize.x <= 0.0 || surface->m_current.bufferSize.y <= 0.0)
        return Vector2D(-1, -1);

    return Vector2D(surface->m_current.viewport.source.x / surface->m_current.bufferSize.x,
                    surface->m_current.viewport.source.y / surface->m_current.bufferSize.y);
}

Vector2D surface_uv_bottom_right(SP<CWLSurfaceResource> surface) {
    if (!surface || !surface->m_current.viewport.hasSource || surface->m_current.bufferSize.x <= 0.0 || surface->m_current.bufferSize.y <= 0.0)
        return Vector2D(-1, -1);

    return Vector2D((surface->m_current.viewport.source.x + surface->m_current.viewport.source.width) / surface->m_current.bufferSize.x,
                    (surface->m_current.viewport.source.y + surface->m_current.viewport.source.height) / surface->m_current.bufferSize.y);
}

// Overview draws directly from the window's live wl_surface tree. This avoids
// depending on Hyprland's private snapshot framebuffer cache, which is no
// longer exposed through the 0.55 render API.
bool draw_window_surface_tree(const SceneTarget& target, const Box& box, const Box& bounds) {
    if (!target.window)
        return false;

    const auto clipped = intersectBox(box, bounds);
    if (!clipped || clipped->w <= 4.0 || clipped->h <= 4.0)
        return false;

    const auto monitor = g_pHyprRenderer->m_renderData.pMonitor.lock();
    const auto sourceMonitor = target.window->m_monitor.lock();
    if (!monitor || !sourceMonitor || sourceMonitor->m_id != monitor->m_id)
        return false;

    const auto rootSurface = target.window->wlSurface();
    const auto rootResource = rootSurface ? rootSurface->resource() : nullptr;
    const auto mainSurface = resolve_window_main_surface(target.window);
    if (!rootResource || !mainSurface)
        return false;

    const auto texture = mainSurface->m_current.texture;
    if (!texture)
        return false;

    const auto sourceBox = preview_surface_source_box(target.window);
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

    // Save every mutable render-state field we touch so overview drawing leaves
    // the surrounding render pass exactly as it found it.
    const ScopedRenderDataState renderStateGuard;

    // Use explicit viewport UVs when the surface has a source crop; otherwise
    // leave custom UV disabled so Hyprland samples the full texture.
    const auto surfaceUVTL = surface_uv_top_left(mainSurface);
    const auto surfaceUVBR = surface_uv_bottom_right(mainSurface);
    g_pHyprRenderer->m_renderData.primarySurfaceUVTopLeft = surfaceUVTL;
    g_pHyprRenderer->m_renderData.primarySurfaceUVBottomRight = surfaceUVBR;
    g_pHyprRenderer->m_renderData.currentWindow = nullptr;

    const auto shape = preview_shape_for_window(target, {centeredTarget.x, centeredTarget.y, centeredTarget.width, centeredTarget.height});
    CHyprOpenGLImpl::STextureRenderData data;
    data.a = 1.0F;
    data.round = shape.round;
    data.roundingPower = shape.roundingPower;
    data.allowCustomUV = surfaceUVTL.x >= 0.0 && surfaceUVBR.x >= 0.0;
    data.blockBlurOptimization = true;
    g_pHyprOpenGL->renderTexture(texture, centeredTarget, data);

    rootResource->breadthfirst(
        [&](SP<CWLSurfaceResource> surface, const Vector2D& offset, void*) {
            if (!surface || surface == mainSurface)
                return;

            const auto subsurfaceTexture = surface->m_current.texture;
            if (!subsurfaceTexture || surface->m_current.size.x <= 1.0 || surface->m_current.size.y <= 1.0)
                return;

            const auto drawBox = CBox{
                centeredTarget.x + offset.x * scale,
                centeredTarget.y + offset.y * scale,
                std::max(1.0, surface->m_current.size.x * scale),
                std::max(1.0, surface->m_current.size.y * scale),
            };
            const auto clippedDrawBox = intersectBox({drawBox.x, drawBox.y, drawBox.width, drawBox.height}, bounds);
            if (!clippedDrawBox)
                return;

            const auto surfaceUVTL = surface_uv_top_left(surface);
            const auto surfaceUVBR = surface_uv_bottom_right(surface);
            g_pHyprRenderer->m_renderData.primarySurfaceUVTopLeft = surfaceUVTL;
            g_pHyprRenderer->m_renderData.primarySurfaceUVBottomRight = surfaceUVBR;
            g_pHyprRenderer->m_renderData.currentWindow = nullptr;

            CHyprOpenGLImpl::STextureRenderData subsurfaceData;
            subsurfaceData.a = 1.0F;
            subsurfaceData.allowCustomUV = surfaceUVTL.x >= 0.0 && surfaceUVBR.x >= 0.0;
            subsurfaceData.blockBlurOptimization = true;
            g_pHyprOpenGL->renderTexture(subsurfaceTexture, to_cbox(*clippedDrawBox), subsurfaceData);
        },
        nullptr);

    return true;
}

void draw_window_preview(RenderState& state, const SceneTarget& target, const Box& drawBox, const Box& bounds, double overlayAlpha) {
    // The preview draw order is:
    // 1. shadow
    // 2. live surface, then matte fallback
    // 3. outline
    // 4. title backdrop + text
    const auto previewBox = insetBox(drawBox, Style::kPreviewInset, Style::kPreviewInset);
    const auto previewShape = preview_shape_for_window(target, previewBox);
    const auto border = Style::previewBorder(target.selected, static_cast<float>(0.92 * overlayAlpha));
    const auto shadowAlpha = target.selected ? 0.22F : 0.14F;
    if (const auto shadowBox = intersectBox(previewBox, bounds); shadowBox && shadowBox->w > 4.0 && shadowBox->h > 4.0) {
        g_pHyprOpenGL->renderRoundedShadow(to_cbox(*shadowBox), previewShape.round, previewShape.roundingPower, 18,
                                           Style::previewShadow(shadowAlpha * overlayAlpha), 1.0F);
    }

    if (!draw_window_surface_tree(target, previewBox, bounds)) {
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
    (void)target;
    (void)bounds;
    (void)overlayAlpha;
}

} // namespace

void snapshotWindowTargets(const Model& model) {
    // Window previews are drawn from live surface textures. Keep this lifecycle
    // hook so session setup remains explicit even though 0.56 no longer exposes
    // the old renderer snapshot cache.
    (void)model;
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

        const auto surface = layer->resource();
        const auto box = layer->logicalBox();
        if (!surface || !surface->m_current.texture || !box)
            continue;

        CHyprOpenGLImpl::STextureRenderData data;
        data.a = 1.0F;
        data.blockBlurOptimization = true;
        g_pHyprOpenGL->renderTexture(
            surface->m_current.texture,
            CBox{box->x - monitor->m_position.x, box->y - monitor->m_position.y, box->width, box->height},
            data);
    }
}

void drawSceneMonitor(const SceneMonitor& scene, steady_tp now, RenderState& state) {
    const auto progress = state.overlayProgress(scene.monitorId, now, true);
    if (progress <= 0.0)
        return;

    std::optional<Box> selectionBox;

    // Draw in increasing specificity:
    // - workspace targets
    // - synthetic empty target, if any
    // - one monitor-local selection outline on top
    for (const auto& workspace : scene.workspaces) {
        for (const auto& target : workspace.targets) {
            if (target.type == TargetType::Window) {
                const auto drawBox = animated_window_draw_box(state, scene.monitorId, target, progress);
                draw_window_preview(state, target, drawBox, scene.box, progress);
                if (target.selected)
                    selectionBox = drawBox;
            } else {
                draw_empty_target(target, scene.box, progress);
            }
        }
    }

    if (scene.syntheticTarget) {
        draw_empty_target(*scene.syntheticTarget, scene.box, progress);
    }

    if (!selectionBox && scene.selectionBox)
        selectionBox = scaled_target_box(*scene.selectionBox, progress);

    if (selectionBox) {
        const auto animatedSelectionBox = state.animatedSelectionBox(*selectionBox, scene.monitorId, now);
        draw_outline_panel(centerScaleBox(animatedSelectionBox, Style::kSelectionOutlineScale),
                           scene.box,
                           Style::selectionOutline(static_cast<float>(0.96 * progress)),
                           18,
                           2.0F);
    }
}

} // namespace Overview
