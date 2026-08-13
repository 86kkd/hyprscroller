/**
 * @file overview/render/state.cpp
 * @brief Shared runtime state for overview rendering and animation.
 */
#include "overview/render/state.h"

#include <array>

#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/config/ConfigManager.hpp>
#include <hyprland/src/config/shared/animation/AnimationTree.hpp>
#include <hyprland/src/animation/AnimationManager.hpp>
#include <hyprland/src/render/Renderer.hpp>

#include "core/hyprland_runtime.h"
#include "core/window_key.h"
#include "overview/render/animation.h"
#include "overview/scene/geometry_utils.h"
#include "overview/scene/layout.h"

namespace Overview {
namespace {

using ScrollerCore::window_key;

Vector2D box_position(const ScrollerCore::Box& box) {
    return Vector2D(box.x, box.y);
}

Vector2D box_size(const ScrollerCore::Box& box) {
    return Vector2D(std::max(1.0, box.w), std::max(1.0, box.h));
}

bool valid_preview_box(const ScrollerCore::Box& box) {
    return finiteBox(box) && box.w > 1.0 && box.h > 1.0;
}

SP<Hyprutils::Animation::SAnimationPropertyConfig> preview_animation_config() {
    auto& animationTree = Config::animationTree();
    if (!animationTree)
        return nullptr;

    static constexpr std::array kCandidates = {
        "windows",
        "windowsMove",
        "fade",
        "global",
    };

    for (const auto* name : kCandidates) {
        if (auto config = animationTree->getAnimationPropertyConfig(name))
            return config;
    }

    for (const auto& [name, config] : animationTree->getAnimationConfig()) {
        if (!config)
            continue;

        if (name.find("window") != std::string::npos || name.find("fade") != std::string::npos)
            return config;
    }

    return nullptr;
}

void attach_preview_damage_callback(PHLANIMVAR<Vector2D>& animation, int monitorId) {
    if (!animation)
        return;

    animation->setUpdateCallback([monitorId](auto) {
        if (const auto monitor = ScrollerCore::HyprlandRuntime::monitorById(monitorId))
            g_pHyprRenderer->damageMonitor(monitor);
    });
}

} // namespace

void RenderState::clearSessionState() {
    openedAt_.clear();
    liveScenes_.clear();
    backdropSnapshots_.clear();
    selectionPulses_.clear();
    previewAnimations_.clear();
    textCache_.clear();
}

void RenderState::clearAll() {
    clearSessionState();
    textCache_.clear();
    lastOverviewActive_ = false;
}

bool RenderState::lastOverviewActive() const {
    return lastOverviewActive_;
}

void RenderState::setLastOverviewActive(bool active) {
    lastOverviewActive_ = active;
}

void RenderState::markOpened(int monitorId, steady_tp now) {
    openedAt_[monitorId] = now;
}

bool RenderState::openingAnimationActive(int monitorId, steady_tp now, bool overviewActive) const {
    if (!overviewActive)
        return false;

    const auto it = openedAt_.find(monitorId);
    return it != openedAt_.end() && now - it->second < kOpenDuration;
}

double RenderState::overlayProgress(int monitorId, steady_tp now, bool overviewActive) const {
    if (!overviewActive)
        return 0.0;

    const auto it = openedAt_.find(monitorId);
    if (it == openedAt_.end())
        return 1.0;

    const auto elapsed = std::chrono::duration<double>(now - it->second).count();
    return easeOutCubic(elapsed / std::chrono::duration<double>(kOpenDuration).count());
}

void RenderState::setScene(int monitorId, const SceneMonitor& scene) {
    liveScenes_[monitorId] = scene;
}

void RenderState::eraseScene(int monitorId) {
    liveScenes_.erase(monitorId);
}

void RenderState::clearScenes() {
    liveScenes_.clear();
}

const SceneMonitor* RenderState::sceneForMonitor(int monitorId) const {
    const auto it = liveScenes_.find(monitorId);
    return it == liveScenes_.end() ? nullptr : &it->second;
}

const std::unordered_map<int, SceneMonitor>& RenderState::scenes() const {
    return liveScenes_;
}

void RenderState::clearBackdropSnapshots() {
    backdropSnapshots_.clear();
}

void RenderState::appendBackdropSnapshot(int monitorId, SP<Render::IFramebuffer> snapshot) {
    backdropSnapshots_[monitorId].push_back(std::move(snapshot));
}

const std::vector<SP<Render::IFramebuffer>>* RenderState::backdropSnapshotsForMonitor(int monitorId) const {
    const auto it = backdropSnapshots_.find(monitorId);
    return it == backdropSnapshots_.end() ? nullptr : &it->second;
}

void RenderState::updateSelectionPulse(const SceneMonitor& scene, steady_tp now) {
    if (!scene.selectionBox) {
        selectionPulses_.erase(scene.monitorId);
        return;
    }

    const auto it = selectionPulses_.find(scene.monitorId);
    if (it == selectionPulses_.end() || !boxesMatch(it->second.box, *scene.selectionBox)) {
        selectionPulses_[scene.monitorId] = SelectionPulse{
            .box = *scene.selectionBox,
            .startedAt = now,
        };
    }
}

bool RenderState::selectionAnimationActive(int monitorId, steady_tp now) const {
    const auto it = selectionPulses_.find(monitorId);
    return it != selectionPulses_.end() && now - it->second.startedAt < kSelectionDuration;
}

ScrollerCore::Box RenderState::animatedSelectionBox(const ScrollerCore::Box& selectionBox, int monitorId, steady_tp now) const {
    const auto it = selectionPulses_.find(monitorId);
    if (it == selectionPulses_.end())
        return selectionBox;

    const auto elapsed = std::chrono::duration<double>(now - it->second.startedAt).count();
    const auto t = clamp01(elapsed / std::chrono::duration<double>(kSelectionDuration).count());
    const auto pulse = 1.0 + 0.05 * (1.0 - easeOutCubic(t));
    return centerScaleBox(selectionBox, pulse);
}

void RenderState::rebuildPreviewAnimations(const Model& model) {
    previewAnimations_.clear();

    if (!Animation::mgr())
        return;

    const auto config = preview_animation_config();
    if (!config)
        return;

    for (const auto& region : model.monitors()) {
        auto monitor = region.monitor;
        if (!monitor)
            continue;

        for (const auto& workspace : region.workspaces) {
            for (const auto& target : workspace.targets) {
                if (target.type != TargetType::Window || !target.window)
                    continue;

                const auto startBox = localizeGlobalBox(target.sourceBox, monitor->m_position.x, monitor->m_position.y);
                const auto goalBox = localizeGlobalBox(target.box, monitor->m_position.x, monitor->m_position.y);
                if (!valid_preview_box(startBox) || !valid_preview_box(goalBox))
                    continue;

                PreviewAnimation animation;
                animation.monitorId = monitor->m_id;
                animation.window = target.window;

                Animation::mgr()->createAnimation(box_position(startBox), animation.position, config, AVARDAMAGE_NONE);
                Animation::mgr()->createAnimation(box_size(startBox), animation.size, config, AVARDAMAGE_NONE);
                attach_preview_damage_callback(animation.position, monitor->m_id);
                attach_preview_damage_callback(animation.size, monitor->m_id);

                *animation.position = box_position(goalBox);
                *animation.size = box_size(goalBox);
                previewAnimations_[window_key(target.window)] = std::move(animation);
            }
        }
    }
}

bool RenderState::previewAnimationActive(int monitorId) const {
    for (const auto& [_, animation] : previewAnimations_) {
        if (animation.monitorId != monitorId)
            continue;

        if ((animation.position && animation.position->isBeingAnimated()) || (animation.size && animation.size->isBeingAnimated()))
            return true;
    }

    return false;
}

ScrollerCore::Box RenderState::animatedPreviewBox(int monitorId, PHLWINDOW window, const ScrollerCore::Box& fallbackBox) const {
    const auto it = previewAnimations_.find(window_key(window));
    if (it == previewAnimations_.end() || it->second.monitorId != monitorId)
        return fallbackBox;

    auto box = fallbackBox;
    if (it->second.position) {
        const auto value = it->second.position->value();
        box.x = value.x;
        box.y = value.y;
    }

    if (it->second.size) {
        const auto value = it->second.size->value();
        box.w = std::max(1.0, value.x);
        box.h = std::max(1.0, value.y);
    }

    return valid_preview_box(box) ? box : fallbackBox;
}

SP<Render::ITexture> RenderState::findTextTexture(const std::string& key) const {
    const auto it = textCache_.find(key);
    return it == textCache_.end() ? nullptr : it->second;
}

void RenderState::storeTextTexture(std::string key, SP<Render::ITexture> texture) {
    textCache_[std::move(key)] = std::move(texture);
}

void RenderState::clearTextCache() {
    textCache_.clear();
}

RenderState& renderState() {
    static RenderState state;
    return state;
}

} // namespace Overview
