#pragma once

#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

#include <hyprland/src/layout/algorithm/TiledAlgorithm.hpp>
#include <hyprland/src/layout/target/Target.hpp>

#include "layout/grid/grid.h"

namespace ScrollerGrid {

class GridLayout final : public Layout::ITiledAlgorithm {
public:
    GridLayout() = default;
    ~GridLayout() override = default;

    void                             newTarget(SP<Layout::ITarget> target) override;
    void                             movedTarget(SP<Layout::ITarget> target, std::optional<Vector2D> focalPoint = std::nullopt) override;
    void                             removeTarget(SP<Layout::ITarget> target) override;
    void                             resizeTarget(const Vector2D& delta, SP<Layout::ITarget> target, Layout::eRectCorner corner = Layout::CORNER_NONE) override;
    void                             recalculate() override;
    std::expected<void, std::string>  layoutMsg(const std::string_view& message) override;
    std::optional<Vector2D>          predictSizeForNewTarget() override;
    SP<Layout::ITarget>              getNextCandidate(SP<Layout::ITarget> old) override;
    void                             swapTargets(SP<Layout::ITarget> a, SP<Layout::ITarget> b) override;
    void                             moveTargetInDirection(SP<Layout::ITarget> target, Math::eDirection direction, bool silent = false) override;

private:
    PHLMONITOR resolve_monitor() const;
    PHLWINDOW active_window() const;
    GridProfile current_profile(PHLMONITOR monitor) const;
    void relayout(PHLMONITOR monitor);

    GridModel model;
    GridViewport viewport;
    std::unordered_map<uintptr_t, PHLWINDOW> windowsByKey;
};

} // namespace ScrollerGrid
