/**
 * @file render_state.h
 * @brief Shared runtime state for overview rendering and animation.
 */
#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include <hyprland/src/desktop/view/LayerSurface.hpp>
#include <hyprland/src/render/OpenGL.hpp>

#include "animation.h"
#include "scene.h"

namespace Overview {

struct SelectionPulse {
    ScrollerCore::Box box;
    steady_tp         startedAt;
};

class RenderState {
  public:
    void clearSessionState();
    void clearAll();

    bool lastOverviewActive() const;
    void setLastOverviewActive(bool active);

    void markOpened(int monitorId, steady_tp now);
    bool openingAnimationActive(int monitorId, steady_tp now, bool overviewActive) const;
    double overlayProgress(int monitorId, steady_tp now, bool overviewActive) const;

    void setScene(int monitorId, const SceneMonitor& scene);
    void eraseScene(int monitorId);
    void clearScenes();
    const SceneMonitor* sceneForMonitor(int monitorId) const;
    const std::unordered_map<int, SceneMonitor>& scenes() const;

    void clearBackdropLayers();
    void appendBackdropLayer(int monitorId, PHLLSREF layer);
    const std::vector<PHLLSREF>* backdropLayersForMonitor(int monitorId) const;

    void updateSelectionPulse(const SceneMonitor& scene, steady_tp now);
    bool selectionAnimationActive(int monitorId, steady_tp now) const;
    ScrollerCore::Box animatedSelectionBox(const ScrollerCore::Box& selectionBox, int monitorId, steady_tp now) const;

    SP<CTexture> findTextTexture(const std::string& key) const;
    void         storeTextTexture(std::string key, SP<CTexture> texture);
    void         clearTextCache();

  private:
    std::unordered_map<int, steady_tp>              openedAt_;
    std::unordered_map<int, SceneMonitor>           liveScenes_;
    std::unordered_map<int, std::vector<PHLLSREF>>  backdropLayers_;
    std::unordered_map<int, SelectionPulse>         selectionPulses_;
    std::unordered_map<std::string, SP<CTexture>>   textCache_;
    bool                                            lastOverviewActive_ = false;
};

RenderState& renderState();

} // namespace Overview
