/**
 * @file overview/render/state.h
 * @brief Shared runtime state for overview rendering and animation.
 */
#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include <hyprland/src/helpers/AnimatedVariable.hpp>
#include <hyprland/src/render/Framebuffer.hpp>
#include <hyprland/src/render/OpenGL.hpp>

#include "overview/render/animation.h"
#include "overview/scene/scene.h"

namespace Overview {

struct SelectionPulse {
    ScrollerCore::Box box;
    steady_tp         startedAt;
};

struct PreviewAnimation {
    int                 monitorId = INVALID_MONITOR_ID;
    PHLWINDOWREF        window;
    PHLANIMVAR<Vector2D> position;
    PHLANIMVAR<Vector2D> size;
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

    void clearBackdropSnapshots();
    void appendBackdropSnapshot(int monitorId, SP<Render::IFramebuffer> snapshot);
    const std::vector<SP<Render::IFramebuffer>>* backdropSnapshotsForMonitor(int monitorId) const;

    void updateSelectionPulse(const SceneMonitor& scene, steady_tp now);
    bool selectionAnimationActive(int monitorId, steady_tp now) const;
    ScrollerCore::Box animatedSelectionBox(const ScrollerCore::Box& selectionBox, int monitorId, steady_tp now) const;

    void rebuildPreviewAnimations(const Model& model);
    bool previewAnimationActive(int monitorId) const;
    ScrollerCore::Box animatedPreviewBox(int monitorId, PHLWINDOW window, const ScrollerCore::Box& fallbackBox) const;

    SP<Render::ITexture> findTextTexture(const std::string& key) const;
    void                 storeTextTexture(std::string key, SP<Render::ITexture> texture);
    void         clearTextCache();

  private:
    std::unordered_map<int, steady_tp>              openedAt_;
    std::unordered_map<int, SceneMonitor>           liveScenes_;
    std::unordered_map<int, std::vector<SP<Render::IFramebuffer>>> backdropSnapshots_;
    std::unordered_map<int, SelectionPulse>         selectionPulses_;
    std::unordered_map<std::uintptr_t, PreviewAnimation> previewAnimations_;
    std::unordered_map<std::string, SP<Render::ITexture>> textCache_;
    bool                                            lastOverviewActive_ = false;
};

RenderState& renderState();

} // namespace Overview
