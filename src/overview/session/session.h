/**
 * @file session.h
 * @brief Global logical overview session spanning all canvases and monitors.
 *
 * The overview session is a navigation-only layer. Real windows remain owned by
 * their per-workspace `CanvasLayout`; overview builds a temporary logical map
 * of windows and empty-workspace targets so directional navigation can happen
 * without mutating real Hyprland focus on every keypress.
 */
#pragma once

#include "core/direction.h"
#include "overview/model/model.h"
#include "overview/navigation/selection.h"

namespace Overview {

class Session {
  public:
    bool active() const;
    void open();
    void close(bool acceptSelectionFlag);
    void dismiss();
    bool moveSelection(Direction direction);
    bool moveCanvasSelection(Direction direction);
    void markInputHandled();
    bool consumeInputHandled(bool released);
    const Model& model() const;
    void damageMonitors() const;

  private:
    bool selectInitialTarget();
    bool selectCanvas(int canvasId, std::optional<int> preferredMonitorId = std::nullopt);
    bool activateCanvas(int canvasId, int selectedMonitorId, bool requireSelectedMonitorFocus, const char* context);
    bool finalizeCanvasTarget(const Target& target, bool warpCursor);
    bool finalizeCanvasOrigin(const OriginState& origin);
    bool acceptSelection();
    bool restoreOrigin();
    std::optional<TargetRef> findBestTarget(Direction direction) const;
    void clear();

    bool                  active_ = false;
    InputHandlingState    inputHandling_;
    Model                 model_;
    int                   originCanvasId_ = INVALID_CANVAS_ID;
    int                   viewCanvasId_ = INVALID_CANVAS_ID;
    std::vector<CanvasLayoutState::SyntheticCanvasWorkspace> pendingCanvases_;
};

Session& session();

} // namespace Overview
