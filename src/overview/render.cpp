/**
 * @file render.cpp
 * @brief Stable overview renderer built on top of Hyprland render passes.
 *
 * The overview renderer intentionally avoids symbol scanning, renderer hooks,
 * and live window framebuffer capture. It draws a monitor-local overlay from
 * the read-only overview session model so overview stays a navigation layer
 * instead of mutating real window geometry during rendering.
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
#include <hyprland/src/desktop/Workspace.hpp>
#include <hyprland/src/event/EventBus.hpp>
#include <hyprland/src/helpers/Color.hpp>
#include <hyprland/src/plugins/PluginAPI.hpp>
#include <hyprland/src/render/OpenGL.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <spdlog/spdlog.h>

#include "../core/layout_math.h"
#include "pass_element.h"
#include "session.h"

namespace Overview {
namespace {

using ScrollerCore::Box;
using ScrollerCore::OverviewProjection;
using steady_tp = std::chrono::steady_clock::time_point;

constexpr auto kOpenDuration = std::chrono::milliseconds(180);
constexpr auto kCloseDuration = std::chrono::milliseconds(180);
constexpr auto kSelectionDuration = std::chrono::milliseconds(120);

struct SceneTarget {
    TargetType type = TargetType::Window;
    WORKSPACEID workspaceId = WORKSPACE_INVALID;
    PHLWINDOW   window = nullptr;
    Box         box;
    bool        synthetic = false;
    bool        selected = false;
    std::string label;
};

struct SceneWorkspace {
    WORKSPACEID             workspaceId = WORKSPACE_INVALID;
    Box                     box;
    Box                     contentBox;
    bool                    selected = false;
    bool                    special = false;
    std::string             label;
    std::vector<SceneTarget> targets;
};

struct SceneMonitor {
    int                         monitorId = MONITOR_INVALID;
    std::string                 monitorName;
    Box                         box;
    std::vector<SceneWorkspace> workspaces;
    std::optional<SceneTarget>  syntheticTarget;
    std::optional<Box>          selectionBox;
};

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
std::unordered_map<int, steady_tp>         g_openedAt;
std::unordered_map<int, ClosingScene>      g_closingScenes;
std::unordered_map<int, SceneMonitor>      g_liveScenes;
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

Box localize_box(PHLMONITOR monitor, const Box& box) {
    if (!monitor)
        return box;

    return {
        box.x - monitor->m_position.x,
        box.y - monitor->m_position.y,
        box.w,
        box.h,
    };
}

CBox to_cbox(const Box& box) {
    return CBox{
        static_cast<double>(iround(box.x)),
        static_cast<double>(iround(box.y)),
        static_cast<double>(std::max(1, iround(box.w))),
        static_cast<double>(std::max(1, iround(box.h))),
    };
}

const MonitorRegion* region_for_monitor(const Model& model, int monitorId) {
    return model.regionForMonitor(monitorId);
}

bool target_matches_selection(const Target& target, const Target* selection) {
    if (!selection)
        return false;

    return target.type == selection->type
        && target.workspaceId == selection->workspaceId
        && target.monitorId == selection->monitorId
        && target.window == selection->window
        && target.synthetic == selection->synthetic;
}

std::string workspace_label(PHLWORKSPACE workspace, WORKSPACEID workspaceId) {
    if (!workspace)
        return std::to_string(workspaceId);

    if (!workspace->m_name.empty())
        return workspace->m_name;

    return std::to_string(workspace->m_id);
}

std::string empty_target_label(const Target& target) {
    return target.synthetic ? "new workspace " + std::to_string(target.workspaceId)
                            : "empty workspace";
}

std::string window_target_label(PHLWINDOW window) {
    if (!window)
        return "window";

    if (!window->m_title.empty())
        return window->m_title;

    return "window";
}

OverviewProjection compute_projection(std::span<const Box> items, const Box& visibleBox) {
    std::vector<ScrollerCore::OverviewRect> rects;
    rects.reserve(items.size());

    for (const auto& item : items) {
        rects.push_back({
            .x0 = item.x,
            .x1 = item.x + item.w,
            .y0 = item.y,
            .y1 = item.y + item.h,
        });
    }

    return ScrollerCore::compute_overview_projection(rects, visibleBox);
}

Box apply_projection(const Box& source, const Box& visibleBox, const OverviewProjection& projection) {
    if (projection.width <= 0.0 || projection.height <= 0.0)
        return visibleBox;

    return {
        visibleBox.x + projection.offset.x + (source.x - projection.min.x) * projection.scale,
        visibleBox.y + projection.offset.y + (source.y - projection.min.y) * projection.scale,
        std::max(24.0, source.w * projection.scale),
        std::max(24.0, source.h * projection.scale),
    };
}

SceneTarget build_empty_workspace_target(const WorkspaceNode& workspace, const Box& contentBox, const Target& target, const Target* selection) {
    return {
        .type = target.type,
        .workspaceId = workspace.workspaceId,
        .window = nullptr,
        .box = inset_box(contentBox, std::max(12.0, contentBox.w * 0.12), std::max(12.0, contentBox.h * 0.14)),
        .synthetic = target.synthetic,
        .selected = target_matches_selection(target, selection),
        .label = empty_target_label(target),
    };
}

std::vector<SceneTarget> build_projected_targets(PHLMONITOR monitor, const WorkspaceNode& workspace, const Box& contentBox, const Target* selection) {
    std::vector<Box> sourceBoxes;
    sourceBoxes.reserve(workspace.targets.size());
    for (const auto& target : workspace.targets)
        sourceBoxes.push_back(localize_box(monitor, target.box));

    const auto projection = compute_projection(sourceBoxes, contentBox);

    std::vector<SceneTarget> targets;
    targets.reserve(workspace.targets.size());
    for (std::size_t index = 0; index < workspace.targets.size(); ++index) {
        const auto& target = workspace.targets[index];
        targets.push_back({
            .type = target.type,
            .workspaceId = target.workspaceId,
            .window = target.window,
            .box = apply_projection(sourceBoxes[index], contentBox, projection),
            .synthetic = target.synthetic,
            .selected = target_matches_selection(target, selection),
            .label = target.type == TargetType::Window ? window_target_label(target.window) : empty_target_label(target),
        });
    }

    return targets;
}

std::optional<SceneMonitor> build_scene_for_monitor(PHLMONITOR monitor) {
    if (!monitor || !session().active())
        return std::nullopt;

    const auto& model = session().model();
    const auto* region = region_for_monitor(model, monitor->m_id);
    if (!region)
        return std::nullopt;

    const auto* selection = model.selection();
    SceneMonitor scene;
    scene.monitorId = monitor->m_id;
    scene.monitorName = monitor->m_name;
    scene.box = {0.0, 0.0, monitor->m_size.x, monitor->m_size.y};

    for (const auto& workspace : region->workspaces) {
        SceneWorkspace sceneWorkspace;
        sceneWorkspace.workspaceId = workspace.workspaceId;
        sceneWorkspace.box = localize_box(monitor, workspace.box);
        sceneWorkspace.contentBox = inset_box(sceneWorkspace.box, 14.0, 14.0);
        sceneWorkspace.contentBox.y += 26.0;
        sceneWorkspace.contentBox.h = std::max(36.0, sceneWorkspace.contentBox.h - 26.0);

        const auto workspaceRef = g_pCompositor->getWorkspaceByID(workspace.workspaceId);
        sceneWorkspace.special = workspaceRef ? workspaceRef->m_isSpecialWorkspace : false;
        sceneWorkspace.label = workspace_label(workspaceRef, workspace.workspaceId);

        const auto hasWindowTargets = std::any_of(workspace.targets.begin(), workspace.targets.end(), [](const Target& target) {
            return target.type == TargetType::Window && target.window;
        });

        if (hasWindowTargets) {
            sceneWorkspace.targets = build_projected_targets(monitor, workspace, sceneWorkspace.contentBox, selection);
        } else if (!workspace.targets.empty()) {
            sceneWorkspace.targets.push_back(build_empty_workspace_target(workspace, sceneWorkspace.contentBox, workspace.targets.front(), selection));
        }

        for (const auto& target : sceneWorkspace.targets) {
            if (!target.selected)
                continue;

            sceneWorkspace.selected = true;
            scene.selectionBox = target.box;
        }

        scene.workspaces.push_back(std::move(sceneWorkspace));
    }

    if (const auto synthetic = model.syntheticSelection(); synthetic && synthetic->monitorId == monitor->m_id) {
        SceneTarget syntheticTarget;
        syntheticTarget.type = synthetic->type;
        syntheticTarget.workspaceId = synthetic->workspaceId;
        syntheticTarget.window = synthetic->window;
        syntheticTarget.box = localize_box(monitor, synthetic->box);
        syntheticTarget.synthetic = true;
        syntheticTarget.selected = target_matches_selection(*synthetic, selection);
        syntheticTarget.label = empty_target_label(*synthetic);
        if (syntheticTarget.selected)
            scene.selectionBox = syntheticTarget.box;
        scene.syntheticTarget = std::move(syntheticTarget);
    }

    return scene;
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

void draw_window_preview(const SceneTarget& target, const Box& bounds, double overlayAlpha) {
    const auto drawBox = center_scale_box(target.box, 0.97 + 0.03 * overlayAlpha);
    const auto baseFill = target.selected ? CHyprColor(0.27F, 0.41F, 0.55F, static_cast<float>(0.88 * overlayAlpha))
                                          : CHyprColor(0.17F, 0.19F, 0.24F, static_cast<float>(0.86 * overlayAlpha));
    const auto border = target.selected ? CHyprColor(0.64F, 0.86F, 0.98F, static_cast<float>(0.92 * overlayAlpha))
                                        : CHyprColor(0.28F, 0.31F, 0.38F, static_cast<float>(0.92 * overlayAlpha));
    draw_panel(drawBox, bounds, border, baseFill, 16, 2.0);

    const auto titleBox = inset_box(drawBox, 10.0, 8.0);
    draw_text(target.label, {titleBox.x, titleBox.y, titleBox.w, 18.0}, bounds, CHyprColor(0.95F, 0.97F, 1.0F, static_cast<float>(overlayAlpha)), 15, 500);
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

void draw_scene_monitor(const SceneMonitor& scene, steady_tp now) {
    const auto progress = overlay_progress(scene.monitorId, now);
    if (progress <= 0.0)
        return;

    const auto overlayBox = center_scale_box(scene.box, 0.965 + 0.035 * progress);
    const auto overlayAlpha = progress;

    draw_rect(scene.box, scene.box, CHyprColor(0.05F, 0.06F, 0.08F, static_cast<float>(0.76 * overlayAlpha)), 0);
    draw_text(scene.monitorName.empty() ? "monitor" : scene.monitorName,
              {18.0, 14.0, std::max(80.0, scene.box.w - 36.0), 20.0},
              scene.box,
              CHyprColor(0.84F, 0.88F, 0.94F, static_cast<float>(overlayAlpha)),
              16,
              500);

    for (const auto& workspace : scene.workspaces) {
        const auto workspaceBox = center_scale_box(workspace.box, 0.97 + 0.03 * progress);
        const auto border = workspace.selected ? CHyprColor(0.53F, 0.74F, 0.88F, static_cast<float>(0.92 * overlayAlpha))
                                               : CHyprColor(0.20F, 0.23F, 0.28F, static_cast<float>(0.92 * overlayAlpha));
        const auto fill = workspace.special ? CHyprColor(0.12F, 0.16F, 0.21F, static_cast<float>(0.88 * overlayAlpha))
                                            : CHyprColor(0.09F, 0.10F, 0.13F, static_cast<float>(0.88 * overlayAlpha));
        draw_panel(workspaceBox, scene.box, border, fill, 24, 2.0);

        draw_text(workspace.label,
                  {workspaceBox.x + 12.0, workspaceBox.y + 8.0, std::max(60.0, workspaceBox.w - 24.0), 18.0},
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
        g_selectionPulses.clear();
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
    const auto scene = build_scene_for_monitor(monitor);
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
    if (g_renderPreListener || g_renderStageListener)
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

        spdlog::info("overview_renderer_init: using render-pass overlay backend");
        return true;
    } catch (const std::exception& e) {
        g_renderPreListener = nullptr;
        g_renderStageListener = nullptr;
        g_openedAt.clear();
        g_closingScenes.clear();
        g_liveScenes.clear();
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
    g_openedAt.clear();
    g_closingScenes.clear();
    g_liveScenes.clear();
    g_selectionPulses.clear();
    g_textCache.clear();
    g_lastOverviewActive = false;
    g_pHyprRenderer->m_renderPass.removeAllOfType("OverviewPassElement");
    spdlog::info("overview_renderer_shutdown");
}

} // namespace Overview
