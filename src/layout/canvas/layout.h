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
#include <utility>

#include <hyprland/src/layout/algorithm/TiledAlgorithm.hpp>
#include <hyprland/src/layout/target/Target.hpp>
#include <hyprland/src/helpers/signal/Signal.hpp>

#include "../../list.h"

enum class Direction { Left, Right, Up, Down, Begin, End, Center };
enum class FitSize { Active, Visible, All, ToEnd, ToBeg };
enum class Mode { Row, Column };
enum class ActiveLaneSyncPolicy { None, WorkspaceFocus };

class Lane;

/**
 * @brief Tiled layout controller for one canvas/workspace instance.
 *
 * `CanvasLayout` is the integration layer between Hyprland's tiled algorithm
 * API and the plugin's internal model. It owns the ordered set of lanes for a
 * canvas, keeps active-lane state in sync with Hyprland focus, and exposes the
 * dispatcher-facing operations used by the plugin.
 */
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
    // Resolve the workspace that owns this canvas instance.
    PHLWORKSPACE getCanvasWorkspace() const;
    // Return the currently active lane, defaulting to the first lane when needed.
    Lane *getActiveLane();
    // Point the canvas at a new active lane.
    void setActiveLane(Lane *lane);
    // Find the lane that currently owns a window.
    Lane *getLaneForWindow(PHLWINDOW window);
    // Return the list node for a lane inside this canvas.
    ListNode<Lane *> *getLaneNode(Lane *lane) const;
    // Return the zero-based index of a lane for logs and paging math.
    int laneIndexOf(Lane *lane) const;
    // Count lanes in the current canvas.
    size_t laneCount() const;
    // Resolve the monitor currently showing this canvas.
    PHLMONITOR getVisibleCanvasMonitor(PHLMONITOR fallbackMonitor = nullptr) const;
    // Relayout this canvas on its visible monitor.
    void relayoutVisibleCanvas(PHLMONITOR fallbackMonitor = nullptr);
    // Recalculate all lanes inside the canvas against one monitor.
    void relayoutCanvas(PHLMONITOR monitor, bool honor_fullscreen);
    // Sync active lane/window state from Hyprland's remembered workspace focus.
    void syncActiveStateFromWorkspaceFocus();
    // Adopt the lane containing a newly focused window.
    bool adoptFocusedLane(PHLWINDOW focusedWindow, PHLMONITOR fallbackMonitor = nullptr);
    // Drop an empty lane and resolve a valid replacement active lane.
    bool dropEmptyLane(ListNode<Lane *> *laneNode, Lane *preferredLane = nullptr, PHLMONITOR fallbackMonitor = nullptr, bool ephemeralOnly = false);
    // Compatibility wrapper used by older ephemeral-lane call sites.
    bool dropEmptyEphemeralLane(ListNode<Lane *> *laneNode, Lane *preferredLane = nullptr, PHLMONITOR fallbackMonitor = nullptr);
    // Choose the active lane that should survive after lane removal.
    Lane *resolveActiveLaneAfterRemoval(ListNode<Lane *> *laneNode, PHLWINDOW removedWindow);

    template <typename Fn>
    // Execute a command against the current active lane with optional focus sync.
    void withActiveLane(ActiveLaneSyncPolicy syncPolicy, Fn&& fn) {
        if (syncPolicy == ActiveLaneSyncPolicy::WorkspaceFocus)
            syncActiveStateFromWorkspaceFocus();

        if (auto *lane = getActiveLane())
            std::forward<Fn>(fn)(lane);
    }

    // Optional Hyprland focus listener used to keep canvas state synchronized.
    CHyprSignalListener m_focusCallback;
    // Currently active lane inside this canvas.
    ListNode<Lane *> *activeLane = nullptr;
    // Ordered lanes that make up the current canvas.
    List<Lane *> lanes;
    // One-shot guard used to avoid immediately re-syncing stale workspace focus
    // after the plugin itself has just moved focus.
    bool suppressNextWorkspaceFocusSync = false;
};
