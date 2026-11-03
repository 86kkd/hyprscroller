/**
 * @file overview/render/draw.h
 * @brief Rendering helpers for overview scene drawing and snapshot setup.
 */
#pragma once

#include "overview/model/model.h"
#include "overview/render/state.h"
#include "overview/scene/scene.h"

namespace Render {
class CRenderContext;
}

namespace Overview {

void snapshotWindowTargets(const Model& model);
void snapshotBackdropLayers(const Model& model, RenderState& state);
void enqueueMonitorBackdrop(Render::CRenderContext& ctx, PHLMONITOR monitor, const RenderState& state);
void drawSceneMonitor(Render::CRenderContext& ctx, const SceneMonitor& scene, steady_tp now, RenderState& state);

} // namespace Overview
