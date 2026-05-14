#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

#include <hyprland/src/layout/algorithm/TiledAlgorithm.hpp>
#include <hyprland/src/layout/target/Target.hpp>

#include "core/overview_snapshot.h"
#include "layout/grid/grid.h"

namespace ScrollerGrid {

class GridLayout final : public Layout::ITiledAlgorithm {
public:
    GridLayout() = default;
    ~GridLayout() override;

    void                             newTarget(SP<Layout::ITarget> target) override;
    void                             movedTarget(SP<Layout::ITarget> target, std::optional<Vector2D> focalPoint = std::nullopt) override;
    void                             removeTarget(SP<Layout::ITarget> target) override;
    void                             resizeTarget(const Vector2D& delta, SP<Layout::ITarget> target, Layout::eRectCorner corner = Layout::CORNER_NONE) override;
    void                             recalculate(Layout::eRecalculateReason reason = Layout::RECALCULATE_REASON_UNKNOWN) override;
    Config::ErrorResult              layoutMsg(const std::string_view& message) override;
    std::optional<Vector2D>          predictSizeForNewTarget() override;
    SP<Layout::ITarget>              getNextCandidate(SP<Layout::ITarget> old) override;
    void                             swapTargets(SP<Layout::ITarget> a, SP<Layout::ITarget> b) override;
    void                             moveTargetInDirection(SP<Layout::ITarget> target, Math::eDirection direction, bool silent = false) override;

    void move_focus(int workspace, Direction direction);
    void move_window(int workspace, Direction direction);
    void cycle_window_size(int workspace, int step);
    void align_window(int workspace, Direction direction);
    void admit_window_left(int workspace);
    void expel_window_right(int workspace);
    void set_mode(int workspace, Mode mode);
    void fit_size(int workspace, FitSize fitSize);
    void toggle_fullscreen(int workspace);
    void create_lane(int workspace, Direction direction);
    void focus_lane(int workspace, Direction direction);
    void focus_window(PHLWINDOW window);
    PHLWINDOW preferred_focus_window(PHLMONITOR monitor, WORKSPACEID workspaceId, Direction direction, PHLWINDOW sourceWindow);
    bool adopt_cross_monitor_window(PHLWINDOW window, PHLMONITOR monitor, bool focusWindow);
    void recalculateMonitor(const int& monitorId);
    void prepareForOverviewSnapshot();
    CanvasOverviewSnapshot buildOverviewSnapshot() const;
    void persistCurrentSnapshot();

private:
    void ensure_workspace_runtime();
    PHLWORKSPACE workspace() const;
    PHLMONITOR resolve_monitor() const;
    PHLMONITOR visible_monitor(PHLMONITOR fallback = nullptr) const;
    PHLWINDOW reference_window() const;
    PHLWINDOW active_window() const;
    void sync_active_from_workspace_focus(PHLMONITOR fallbackMonitor);
    GridProfile current_profile(PHLMONITOR monitor) const;
    bool manage_window(PHLWINDOW window, PHLMONITOR monitor, bool focusNewWindow);
    void relayout(PHLMONITOR monitor);
    void focus_active_window(const char* context);
    std::optional<ScrollerSnapshot::CanvasSnapshot> captureSnapshot() const;
    void persistSnapshot();
    void clearPersistedSnapshot();
    bool maybeRestoreWorkspaceSnapshot();
    bool restoreSnapshot(const ScrollerSnapshot::CanvasSnapshot& snapshot);
    bool handoffFocusAcrossMonitor(int workspace, Direction direction, PHLWINDOW sourceWindow, PHLMONITOR sourceMonitor, PHLMONITOR targetMonitor);
    bool handoffMoveWindowAcrossMonitor(int workspace, Direction direction, PHLWINDOW currentWindow, PHLMONITOR sourceMonitor, PHLMONITOR targetMonitor);

    GridModel model;
    GridViewport viewport;
    std::unordered_map<uintptr_t, PHLWINDOW> windowsByKey;
    WORKSPACEID workspaceRuntimeId = WORKSPACE_INVALID;
    bool restoringSnapshot = false;
    bool snapshotRestoreAttempted = false;
    std::optional<Mode> modeOverride;
    std::optional<uintptr_t> fullscreenKey;
};

} // namespace ScrollerGrid
