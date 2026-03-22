/**
 * @file layout.h
 * @brief Layout controller for Hyprscroller's tiled workspace orchestration.
 *
 * Declares `CanvasLayout`, the Hyprland tiled algorithm implementation that
 * owns the lanes of a canvas, translates Hyprland layout callbacks, and exposes
 * command-facing operations through dispatchers.
 */
#pragma once

#include <expected>
#include <optional>
#include <string>
#include <string_view>

#include <hyprland/src/layout/algorithm/TiledAlgorithm.hpp>
#include <hyprland/src/layout/target/Target.hpp>
#include <hyprland/src/helpers/signal/Signal.hpp>

#include "../../list.h"

enum class Direction { Left, Right, Up, Down, Begin, End, Center };
enum class FitSize { Active, Visible, All, ToEnd, ToBeg };
enum class Mode { Row, Column };

class Lane;

class CanvasLayout : public Layout::ITiledAlgorithm {
public:
    // Public hooks required by Hyprland's tiled algorithm interface.
    void                             newTarget(SP<Layout::ITarget> target) override;
    void                             movedTarget(SP<Layout::ITarget> target, std::optional<Vector2D> focalPoint = std::nullopt) override;
    void                             removeTarget(SP<Layout::ITarget> target) override;
    void                             resizeTarget(const Vector2D &delta, SP<Layout::ITarget> target, Layout::eRectCorner corner = Layout::CORNER_NONE) override;
    void                             recalculate() override;
    std::expected<void, std::string>  layoutMsg(const std::string_view& sv) override;
    std::optional<Vector2D>          predictSizeForNewTarget() override;
    SP<Layout::ITarget>              getNextCandidate(SP<Layout::ITarget> old) override;
    void                             swapTargets(SP<Layout::ITarget> a, SP<Layout::ITarget> b) override;
    void                             moveTargetInDirection(SP<Layout::ITarget> t, Math::eDirection direction, bool silent = false) override;

    // Internal compatibility helpers used by LayoutAlgorithm dispatch and
    // legacy callback paths.
    void onEnable();
    void onDisable();
    // Called when a tiled window is first mapped.
    void onWindowCreatedTiling(PHLWINDOW, Math::eDirection = Math::DIRECTION_DEFAULT);
    // Return true if the layout currently manages this window.
    bool isWindowTiled(PHLWINDOW);
    // Called when a tiled window is unmapped.
    void onWindowRemovedTiling(PHLWINDOW);
    // Recompute geometry for monitor-specific constraints or DPI/workspace changes.
    void recalculateMonitor(const int &monitor_id);
    // Recompute layout for a specific window (for toggles like pseudo and resize).
    void recalculateWindow(PHLWINDOW);
    void resizeActiveWindow(PHLWINDOW, const Vector2D &delta, Layout::eRectCorner = Layout::CORNER_NONE, PHLWINDOW pWindow = nullptr);
    // Move current active item using directional command aliases.
    void moveWindowTo(PHLWINDOW, const std::string &direction, bool silent = false);
    // Swaps cached window metadata when plugin-level mapping changes.
    void switchWindows(PHLWINDOW, PHLWINDOW);
    // Compatibility no-op for split ratio events.
    void alterSplitRatio(PHLWINDOW, float, bool);
    PHLWINDOW getNextWindowCandidate(PHLWINDOW);
    // Keep active lane/stack selection synchronized with focus changes.
    void onWindowFocusChange(PHLWINDOW);
    void replaceWindowDataWith(PHLWINDOW from, PHLWINDOW to);
    Vector2D predictSizeForNewWindowTiled();

    // New dispatchers: command-facing control surface from Hyprland config.
    void cycle_window_size(int workspace, int step);
    void move_focus(int workspace, Direction);
    void move_window(int workspace, Direction);
    void align_window(int workspace, Direction);
    void admit_window_left(int workspace);
    void expel_window_right(int workspace);
    void set_mode(int workspace, Mode);
    void fit_size(int workspace, FitSize);
    void toggle_overview(int workspace);
    void toggle_fullscreen(int workspace);
    void create_lane(int workspace, Direction);
    void focus_lane(int workspace, Direction);

    // Mark helpers: lightweight named bookmarks for focused windows.
    void marks_add(const std::string &name);
    void marks_delete(const std::string &name);
    void marks_visit(const std::string &name);
    void marks_reset();

private:
    PHLWORKSPACE getCanvasWorkspace() const;
    Lane *getActiveLane();
    void setActiveLane(Lane *lane);
    Lane *getLaneForWindow(PHLWINDOW window);
    void relayoutCanvas(PHLMONITOR monitor, bool honor_fullscreen);

    CHyprSignalListener m_focusCallback;
    ListNode<Lane *> *activeLane = nullptr;
    List<Lane *> lanes;
};
