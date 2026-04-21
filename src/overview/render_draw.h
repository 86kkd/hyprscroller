/**
 * @file render_draw.h
 * @brief Rendering helpers for overview scene drawing and snapshot setup.
 */
#pragma once

#include "model.h"
#include "render_state.h"
#include "scene.h"

namespace Overview {

void snapshotWindowTargets(const Model& model);
void snapshotBackdropLayers(const Model& model, RenderState& state);
void enqueueMonitorBackdrop(PHLMONITOR monitor, const RenderState& state);
void drawSceneMonitor(const SceneMonitor& scene, steady_tp now, RenderState& state);

} // namespace Overview
