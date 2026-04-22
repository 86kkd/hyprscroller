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

#include "../core/direction.h"
#include "model.h"
#include "session_selection.h"

namespace Overview {

class Session {
  public:
    bool active() const;
    void open();
    void close(bool acceptSelectionFlag);
    void dismiss();
    bool moveSelection(Direction direction);
    void markInputHandled();
    bool consumeInputHandled(bool released);
    const Model& model() const;
    void damageMonitors() const;

  private:
    bool selectInitialTarget();
    bool acceptSelection();
    bool restoreOrigin();
    bool createSyntheticEmptyTarget(Direction direction);
    std::optional<TargetRef> findBestTarget(Direction direction) const;
    void clear();

    bool                  active_ = false;
    InputHandlingState    inputHandling_;
    Model                 model_;
};

Session& session();

} // namespace Overview
