/**
 * @file overview/render/draw.h
 * @brief Rendering helpers for overview scene drawing and snapshot setup.
 */
#pragma once

#include "overview/model/model.h"
#include "overview/render/state.h"
#include "overview/scene/scene.h"

namespace Overview {

void snapshotWindowTargets(const Model& model);
void snapshotBackdropLayers(const Model& model, RenderState& state);
void enqueueMonitorBackdrop(PHLMONITOR monitor, const RenderState& state);
void drawSceneMonitor(const SceneMonitor& scene, steady_tp now, RenderState& state);

} // namespace Overview
