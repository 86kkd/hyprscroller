/**
 * @file overview/render/state.cpp
 * @brief Shared runtime state for overview rendering and animation.
 */
#include "overview/render/state.h"

#include "overview/render/animation.h"
#include "overview/scene/geometry_utils.h"

namespace Overview {

void RenderState::clearSessionState() {
    openedAt_.clear();
    liveScenes_.clear();
    backdropLayers_.clear();
    selectionPulses_.clear();
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

void RenderState::clearBackdropLayers() {
    backdropLayers_.clear();
}

void RenderState::appendBackdropLayer(int monitorId, PHLLSREF layer) {
    backdropLayers_[monitorId].push_back(std::move(layer));
}

const std::vector<PHLLSREF>* RenderState::backdropLayersForMonitor(int monitorId) const {
    const auto it = backdropLayers_.find(monitorId);
    return it == backdropLayers_.end() ? nullptr : &it->second;
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

SP<CTexture> RenderState::findTextTexture(const std::string& key) const {
    const auto it = textCache_.find(key);
    return it == textCache_.end() ? nullptr : it->second;
}

void RenderState::storeTextTexture(std::string key, SP<CTexture> texture) {
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
