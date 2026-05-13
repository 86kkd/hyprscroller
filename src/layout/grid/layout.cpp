#include "layout/grid/layout.h"

#include <algorithm>

#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/layout/algorithm/Algorithm.hpp>
#include <hyprland/src/layout/space/Space.hpp>
#include <hyprutils/math/Box.hpp>
#include <spdlog/spdlog.h>

#include "core/core.h"
#include "core/window_key.h"
#include "core/workspace_selector.h"
#include "layout/canvas/internal.h"
#include "layout/canvas/layout_repository.h"
#include "plugin/config.h"

namespace ScrollerGrid {
namespace {

std::optional<Direction> direction_from_hypr(Math::eDirection direction) {
    switch (direction) {
    case Math::DIRECTION_LEFT:
        return Direction::Left;
    case Math::DIRECTION_RIGHT:
        return Direction::Right;
    case Math::DIRECTION_UP:
        return Direction::Up;
    case Math::DIRECTION_DOWN:
        return Direction::Down;
    default:
        return std::nullopt;
    }
}

void sync_window_target_geometry(PHLWINDOW window) {
    if (!window)
        return;

    const auto target = window->layoutTarget();
    if (!target)
        return;

    target->setPositionGlobal(Hyprutils::Math::CBox(window->m_position, window->m_size));
}

std::vector<PHLWINDOW> live_tiled_workspace_windows(PHLWORKSPACE workspace) {
    std::vector<PHLWINDOW> windows;
    if (!workspace || !g_pCompositor)
        return windows;

    windows.reserve(g_pCompositor->m_windows.size());
    for (const auto& window : g_pCompositor->m_windows) {
        if (!window || window->workspaceID() != workspace->m_id || window->m_isFloating || !window->m_isMapped || window->isHidden())
            continue;
        windows.push_back(window);
    }
    return windows;
}

GridLayout* grid_for_workspace(WORKSPACEID workspaceId) {
    if (!g_pCompositor)
        return nullptr;

    const auto workspace = g_pCompositor->getWorkspaceByID(workspaceId);
    if (!workspace || !workspace->m_space)
        return nullptr;

    const auto algorithm = workspace->m_space->algorithm();
    if (!algorithm)
        return nullptr;

    const auto& tiled = algorithm->tiledAlgo();
    if (!tiled)
        return nullptr;

    return dynamic_cast<GridLayout*>(tiled.get());
}

bool focus_wrap_enabled() {
    return scroller::plugin_config::focusWrap();
}

std::optional<uintptr_t> snapshot_active_key(const ScrollerSnapshot::GridSnapshot& snapshot) {
    if (snapshot.activeItemIndex < 0 || static_cast<size_t>(snapshot.activeItemIndex) >= snapshot.items.size())
        return std::nullopt;
    return snapshot.items[static_cast<size_t>(snapshot.activeItemIndex)].key;
}

std::optional<Mode> snapshot_grid_mode(const ScrollerSnapshot::GridSnapshot& snapshot) {
    switch (static_cast<Mode>(snapshot.mode)) {
    case Mode::Row:
    case Mode::Column:
        return static_cast<Mode>(snapshot.mode);
    default:
        return std::nullopt;
    }
}

PHLMONITOR monitor_in_direction(PHLMONITOR sourceMonitor, Direction direction) {
    const auto monitorDirection = CanvasLayoutInternal::direction_to_math(direction);
    if (!g_pCompositor || !sourceMonitor || !monitorDirection)
        return nullptr;

    return g_pCompositor->getMonitorInDirection(sourceMonitor, *monitorDirection);
}

} // namespace

GridLayout::~GridLayout() {
    persistSnapshot();
}

PHLWORKSPACE GridLayout::workspace() const {
    const auto algorithm = m_parent.lock();
    const auto space = algorithm ? algorithm->space() : nullptr;
    return space ? space->workspace() : nullptr;
}

void GridLayout::ensure_workspace_runtime() {
    const auto currentWorkspace = workspace();
    if (!currentWorkspace) {
        workspaceRuntimeId = WORKSPACE_INVALID;
        return;
    }

    if (workspaceRuntimeId == currentWorkspace->m_id)
        return;

    if (workspaceRuntimeId != WORKSPACE_INVALID) {
        windowsByKey.clear();
        model.clear();
        viewport = {};
        modeOverride.reset();
        fullscreenKey.reset();
    }

    workspaceRuntimeId = currentWorkspace->m_id;
    snapshotRestoreAttempted = false;
}

PHLMONITOR GridLayout::resolve_monitor() const {
    if (const auto currentWorkspace = workspace()) {
        if (const auto monitor = CanvasLayoutInternal::visible_monitor_for_workspace(currentWorkspace))
            return monitor;
    }

    if (const auto window = reference_window())
        return g_pCompositor->getMonitorFromID(window->monitorID());

    return ScrollerCore::monitorFromPointingOrCursor();
}

PHLMONITOR GridLayout::visible_monitor(PHLMONITOR fallback) const {
    if (const auto currentWorkspace = workspace()) {
        if (const auto monitor = CanvasLayoutInternal::visible_monitor_for_workspace(currentWorkspace))
            return monitor;
    }

    return fallback ? fallback : resolve_monitor();
}

PHLWINDOW GridLayout::reference_window() const {
    if (const auto active = active_window())
        return active;

    for (const auto& [_, window] : windowsByKey) {
        if (window)
            return window;
    }

    return nullptr;
}

PHLWINDOW GridLayout::active_window() const {
    const auto* active = model.active_item();
    if (!active)
        return nullptr;

    const auto it = windowsByKey.find(active->key);
    return it == windowsByKey.end() ? nullptr : it->second;
}

GridProfile GridLayout::current_profile(PHLMONITOR monitor) const {
    if (!monitor)
        return {};

    const auto workarea = CanvasLayoutInternal::compute_canvas_bounds(monitor).max;
    return modeOverride ? profile_for_workarea(*modeOverride, workarea)
                        : profile_for_workarea_extent(workarea);
}

bool GridLayout::manage_window(PHLWINDOW window, PHLMONITOR monitor, bool focusNewWindow) {
    if (!window)
        return false;

    monitor = visible_monitor(monitor ? monitor : g_pCompositor->getMonitorFromID(window->monitorID()));
    const auto profile = current_profile(monitor);
    const auto key = ScrollerCore::window_key(window);
    windowsByKey[key] = window;

    const auto added = model.add_window(key, profile);
    if (!added && focusNewWindow)
        (void)model.focus_window(key);

    model.ensure_active_visible(profile, viewport);
    relayout(monitor);
    persistSnapshot();
    return added;
}

void GridLayout::relayout(PHLMONITOR monitor) {
    monitor = visible_monitor(monitor);
    if (!monitor)
        return;

    const auto bounds = CanvasLayoutInternal::compute_canvas_bounds(monitor);
    const auto profile = current_profile(monitor);
    if (fullscreenKey && !model.contains(*fullscreenKey))
        fullscreenKey.reset();

    const auto currentWorkspace = workspace();
    if (currentWorkspace && currentWorkspace->m_hasFullscreenWindow && currentWorkspace->m_fullscreenMode == FSMODE_FULLSCREEN) {
        if (const auto active = active_window()) {
            active->m_position = {bounds.full.x, bounds.full.y};
            active->m_size = {bounds.full.w, bounds.full.h};
            sync_window_target_geometry(active);
        }
        return;
    }

    for (const auto& item : model.render(viewport, profile, bounds.full, bounds.max)) {
        const auto it = windowsByKey.find(item.key);
        if (it == windowsByKey.end() || !it->second)
            continue;

        const auto box = fullscreenKey && *fullscreenKey == item.key ? bounds.max : item.committedBox;
        it->second->m_position = {box.x, box.y};
        it->second->m_size = {box.w, box.h};
        sync_window_target_geometry(it->second);
    }
}

std::optional<ScrollerSnapshot::CanvasSnapshot> GridLayout::captureSnapshot() const {
    const auto currentWorkspace = workspace();
    if (!currentWorkspace || model.empty())
        return std::nullopt;

    ScrollerSnapshot::CanvasSnapshot snapshot;
    snapshot.workspaceId = currentWorkspace->m_id;
    snapshot.grid = model.capture_snapshot(viewport);
    if (fullscreenKey && model.contains(*fullscreenKey))
        snapshot.grid.fullscreenKey = *fullscreenKey;
    if (const auto monitor = resolve_monitor())
        snapshot.grid.mode = static_cast<int>(current_profile(monitor).mode);
    return snapshot;
}

void GridLayout::persistSnapshot() {
    if (restoringSnapshot)
        return;

    const auto currentWorkspace = workspace();
    if (!currentWorkspace)
        return;

    const auto liveWindows = live_tiled_workspace_windows(currentWorkspace);
    if (!liveWindows.empty()) {
        const auto fullyManaged = std::all_of(liveWindows.begin(), liveWindows.end(), [this](const auto& window) {
            return window && windowsByKey.contains(ScrollerCore::window_key(window));
        });
        if (!fullyManaged) {
            spdlog::debug("grid persistSnapshot: preserving last complete snapshot during partial detach workspace={} live_windows={}",
                          currentWorkspace->m_id,
                          liveWindows.size());
            return;
        }
    }

    if (const auto snapshot = captureSnapshot()) {
        CanvasLayoutState::repository().upsert(*snapshot);
        return;
    }

    clearPersistedSnapshot();
}

void GridLayout::clearPersistedSnapshot() {
    const auto currentWorkspace = workspace();
    if (!currentWorkspace)
        return;

    CanvasLayoutState::repository().erase(currentWorkspace->m_id);
}

bool GridLayout::restoreSnapshot(const ScrollerSnapshot::CanvasSnapshot& snapshot) {
    const auto currentWorkspace = workspace();
    if (!currentWorkspace || snapshot.workspaceId != currentWorkspace->m_id)
        return false;

    const auto liveWindows = live_tiled_workspace_windows(currentWorkspace);
    if (liveWindows.empty())
        return false;

    const auto monitor = visible_monitor(g_pCompositor->getMonitorFromID(liveWindows.front()->monitorID()));
    if (!monitor)
        return false;

    if (snapshot.grid.enabled) {
        if (const auto restoredMode = snapshot_grid_mode(snapshot.grid))
            modeOverride = *restoredMode;
    }
    const auto profile = current_profile(monitor);
    const auto sourceGrid = snapshot.grid.enabled
        ? snapshot.grid
        : migrate_legacy_snapshot_to_grid(snapshot, profile);
    if (!sourceGrid.enabled || sourceGrid.items.empty())
        return false;

    std::unordered_map<uintptr_t, PHLWINDOW> liveByKey;
    liveByKey.reserve(liveWindows.size());
    for (const auto& window : liveWindows)
        liveByKey.emplace(ScrollerCore::window_key(window), window);

    ScrollerSnapshot::GridSnapshot filtered = sourceGrid;
    filtered.items.clear();
    filtered.activeItemIndex = -1;

    const auto activeKey = snapshot_active_key(sourceGrid);
    for (const auto& item : sourceGrid.items) {
        const auto liveIt = liveByKey.find(item.key);
        if (liveIt == liveByKey.end())
            continue;

        if (activeKey && item.key == *activeKey)
            filtered.activeItemIndex = static_cast<int>(filtered.items.size());
        filtered.items.push_back(item);
        liveByKey.erase(liveIt);
    }

    restoringSnapshot = true;
    windowsByKey.clear();
    model.clear();
    viewport = {
        .originColumn = filtered.viewportColumn,
        .originRow = filtered.viewportRow,
    };
    fullscreenKey.reset();

    for (const auto& item : filtered.items) {
        for (const auto& window : liveWindows) {
            if (ScrollerCore::window_key(window) == item.key) {
                windowsByKey[item.key] = window;
                break;
            }
        }
    }

    model.restore_snapshot(filtered);
    if (filtered.fullscreenKey != 0 && model.contains(filtered.fullscreenKey))
        fullscreenKey = filtered.fullscreenKey;
    for (const auto& [key, window] : liveByKey) {
        windowsByKey[key] = window;
        (void)model.add_window(key, profile);
    }

    if (const auto focusedWindow = currentWorkspace->getLastFocusedWindow()) {
        if (focusedWindow->workspaceID() == currentWorkspace->m_id)
            (void)model.focus_window(ScrollerCore::window_key(focusedWindow));
    }

    model.ensure_active_visible(profile, viewport);
    relayout(monitor);
    restoringSnapshot = false;
    persistSnapshot();
    spdlog::info("grid restore_snapshot: workspace={} restored_items={} live_windows={} migrated_legacy={}",
                 currentWorkspace->m_id,
                 sourceGrid.items.size(),
                 liveWindows.size(),
                 !snapshot.grid.enabled);
    return true;
}

bool GridLayout::maybeRestoreWorkspaceSnapshot() {
    if (snapshotRestoreAttempted)
        return false;

    snapshotRestoreAttempted = true;
    const auto currentWorkspace = workspace();
    if (!currentWorkspace || !model.empty())
        return false;

    CanvasLayoutState::repository().initialize();
    const auto snapshot = CanvasLayoutState::repository().find(currentWorkspace->m_id);
    if (!snapshot || (!snapshot->grid.enabled && snapshot->lanes.empty()))
        return false;

    return restoreSnapshot(*snapshot);
}

void GridLayout::newTarget(SP<Layout::ITarget> target) {
    ensure_workspace_runtime();
    const auto window = ScrollerCore::windowFromTarget(target);
    if (!window)
        return;

    const auto monitor = g_pCompositor->getMonitorFromID(window->monitorID());
    const auto key = ScrollerCore::window_key(window);
    (void)maybeRestoreWorkspaceSnapshot();
    if (model.contains(key)) {
        windowsByKey[key] = window;
        relayout(monitor);
        return;
    }

    (void)manage_window(window, monitor, true);
}

void GridLayout::movedTarget(SP<Layout::ITarget> target, std::optional<Vector2D>) {
    newTarget(target);
}

void GridLayout::removeTarget(SP<Layout::ITarget> target) {
    ensure_workspace_runtime();
    const auto window = ScrollerCore::windowFromTarget(target);
    if (!window)
        return;

    const auto monitor = g_pCompositor->getMonitorFromID(window->monitorID());
    const auto key = ScrollerCore::window_key(window);
    windowsByKey.erase(key);
    model.remove_window(key);
    if (fullscreenKey && *fullscreenKey == key)
        fullscreenKey.reset();
    relayout(monitor ? monitor : resolve_monitor());
    persistSnapshot();
}

void GridLayout::resizeTarget(const Vector2D&, SP<Layout::ITarget>, Layout::eRectCorner) {
    relayout(resolve_monitor());
}

void GridLayout::recalculate(Layout::eRecalculateReason) {
    ensure_workspace_runtime();
    (void)maybeRestoreWorkspaceSnapshot();
    relayout(resolve_monitor());
}

Config::ErrorResult GridLayout::layoutMsg(const std::string_view& message) {
    spdlog::warn("grid layoutMsg: unsupported message='{}'", message);
    return Config::configError("grid layout messages are not supported yet", Config::eConfigErrorLevel::ERROR, Config::eConfigErrorCode::INVALID_ARGUMENT);
}

std::optional<Vector2D> GridLayout::predictSizeForNewTarget() {
    const auto monitor = resolve_monitor();
    if (!monitor)
        return {};

    const auto profile = current_profile(monitor);
    return Vector2D(profile.unitWidth, profile.unitHeight);
}

SP<Layout::ITarget> GridLayout::getNextCandidate(SP<Layout::ITarget>) {
    const auto active = active_window();
    return active ? active->layoutTarget() : nullptr;
}

void GridLayout::swapTargets(SP<Layout::ITarget> a, SP<Layout::ITarget> b) {
    ensure_workspace_runtime();
    const auto wa = ScrollerCore::windowFromTarget(a);
    const auto wb = ScrollerCore::windowFromTarget(b);
    if (!wa || !wb)
        return;

    const auto keyA = ScrollerCore::window_key(wa);
    const auto keyB = ScrollerCore::window_key(wb);
    if (!model.swap_windows(keyA, keyB))
        return;

    relayout(resolve_monitor());
    persistSnapshot();
}

void GridLayout::moveTargetInDirection(SP<Layout::ITarget> target, Math::eDirection direction, bool) {
    ensure_workspace_runtime();
    const auto parsed = direction_from_hypr(direction);
    if (!parsed)
        return;

    if (const auto window = ScrollerCore::windowFromTarget(target))
        (void)model.focus_window(ScrollerCore::window_key(window));

    const auto monitor = resolve_monitor();
    const auto profile = current_profile(monitor);
    (void)model.move_focus(*parsed, profile, viewport, focus_wrap_enabled());
    relayout(monitor);
    persistSnapshot();
}

void GridLayout::move_focus(int workspace, Direction direction) {
    ensure_workspace_runtime();
    (void)maybeRestoreWorkspaceSnapshot();

    const auto monitor = resolve_monitor();
    const auto profile = current_profile(monitor);
    const auto before = active_window();
    const auto sourceMonitor = before ? g_pCompositor->getMonitorFromID(before->monitorID()) : monitor;
    const auto targetMonitor = monitor_in_direction(sourceMonitor, direction);
    const auto wrap = focus_wrap_enabled();
    if (!wrap && targetMonitor && model.active_item_at_edge(direction) &&
        handoffFocusAcrossMonitor(workspace, direction, before, sourceMonitor, targetMonitor)) {
        return;
    }

    if (model.move_focus(direction, profile, viewport, wrap) != GridMoveResult::Moved) {
        if (targetMonitor && handoffFocusAcrossMonitor(workspace, direction, before, sourceMonitor, targetMonitor))
            return;
        return;
    }

    relayout(monitor);
    persistSnapshot();
    focus_active_window("grid_move_focus");
}

void GridLayout::move_window(int workspace, Direction direction) {
    ensure_workspace_runtime();
    (void)maybeRestoreWorkspaceSnapshot();

    const auto monitor = resolve_monitor();
    const auto profile = current_profile(monitor);
    const auto currentWindow = active_window();
    const auto sourceMonitor = currentWindow ? g_pCompositor->getMonitorFromID(currentWindow->monitorID()) : monitor;
    const auto targetMonitor = monitor_in_direction(sourceMonitor, direction);
    if (targetMonitor && model.active_item_at_edge(direction) &&
        handoffMoveWindowAcrossMonitor(workspace, direction, currentWindow, sourceMonitor, targetMonitor)) {
        return;
    }

    if (model.move_active_window(direction, profile, viewport) != GridMoveResult::Moved)
        return;

    relayout(monitor);
    persistSnapshot();
    focus_active_window("grid_move_window");
}

void GridLayout::cycle_window_size(int workspace, int step) {
    (void)workspace;
    ensure_workspace_runtime();

    const auto monitor = resolve_monitor();
    const auto profile = current_profile(monitor);
    if (model.resize_active_item(step, profile, viewport) != GridMoveResult::Moved)
        return;

    relayout(monitor);
    persistSnapshot();
}

void GridLayout::align_window(int workspace, Direction direction) {
    (void)workspace;
    ensure_workspace_runtime();

    const auto monitor = resolve_monitor();
    const auto profile = current_profile(monitor);
    if (model.align_active(direction, profile, viewport) != GridMoveResult::Moved)
        return;

    relayout(monitor);
    persistSnapshot();
}

void GridLayout::admit_window_left(int workspace) {
    const auto monitor = resolve_monitor();
    const auto direction = current_profile(monitor).mode == Mode::Column ? Direction::Up : Direction::Left;
    move_window(workspace, direction);
}

void GridLayout::expel_window_right(int workspace) {
    const auto monitor = resolve_monitor();
    const auto direction = current_profile(monitor).mode == Mode::Column ? Direction::Down : Direction::Right;
    create_lane(workspace, direction);
}

void GridLayout::set_mode(int workspace, Mode mode) {
    (void)workspace;
    ensure_workspace_runtime();
    modeOverride = mode;

    const auto monitor = resolve_monitor();
    const auto profile = current_profile(monitor);
    model.ensure_active_visible(profile, viewport);
    relayout(monitor);
    persistSnapshot();
}

void GridLayout::fit_size(int workspace, FitSize fitSize) {
    (void)workspace;
    ensure_workspace_runtime();

    const auto monitor = resolve_monitor();
    const auto profile = current_profile(monitor);
    auto columnSpan = 1;
    auto rowSpan = 1;
    if (fitSize != FitSize::Active) {
        columnSpan = std::max(1, profile.visibleColumns);
        rowSpan = std::max(1, profile.visibleRows);
    }

    if (model.set_active_span(columnSpan, rowSpan, profile, viewport) != GridMoveResult::Moved)
        return;

    relayout(monitor);
    persistSnapshot();
}

void GridLayout::toggle_fullscreen(int workspace) {
    (void)workspace;
    ensure_workspace_runtime();

    const auto* active = model.active_item();
    if (!active)
        return;

    if (fullscreenKey && *fullscreenKey == active->key)
        fullscreenKey.reset();
    else
        fullscreenKey = active->key;

    relayout(resolve_monitor());
    persistSnapshot();
}

void GridLayout::create_lane(int workspace, Direction direction) {
    (void)workspace;
    ensure_workspace_runtime();

    const auto monitor = resolve_monitor();
    const auto profile = current_profile(monitor);
    if (model.move_active_window_to_page(direction, profile, viewport) != GridMoveResult::Moved)
        return;

    relayout(monitor);
    persistSnapshot();
    focus_active_window("grid_create_lane");
}

void GridLayout::focus_lane(int workspace, Direction direction) {
    (void)workspace;
    ensure_workspace_runtime();

    const auto monitor = resolve_monitor();
    const auto profile = current_profile(monitor);
    switch (direction) {
    case Direction::Left:
        viewport.originColumn -= std::max(1, profile.visibleColumns);
        break;
    case Direction::Right:
        viewport.originColumn += std::max(1, profile.visibleColumns);
        break;
    case Direction::Up:
        viewport.originRow -= std::max(1, profile.visibleRows);
        break;
    case Direction::Down:
        viewport.originRow += std::max(1, profile.visibleRows);
        break;
    case Direction::Begin:
        viewport = {};
        break;
    case Direction::End:
        model.ensure_active_visible(profile, viewport);
        break;
    default:
        return;
    }

    relayout(monitor);
    persistSnapshot();
}

void GridLayout::focus_active_window(const char* context) {
    const auto window = active_window();
    if (!window)
        return;

    if (!CanvasLayoutInternal::switch_to_window(window, true)) {
        spdlog::warn("{}: failed to focus grid window window={} workspace={} monitor={}",
                     context ? context : "grid_focus",
                     static_cast<const void*>(window.get()),
                     window->workspaceID(),
                     window->monitorID());
    }
}

void GridLayout::focus_window(PHLWINDOW window) {
    ensure_workspace_runtime();
    if (!window)
        return;

    const auto key = ScrollerCore::window_key(window);
    if (!model.focus_window(key))
        return;

    const auto monitor = g_pCompositor->getMonitorFromID(window->monitorID());
    model.ensure_active_visible(current_profile(monitor), viewport);
    relayout(monitor);
    persistSnapshot();
}

PHLWINDOW GridLayout::preferred_focus_window(PHLMONITOR monitor,
                                             WORKSPACEID workspaceId,
                                             Direction direction,
                                             PHLWINDOW sourceWindow) {
    ensure_workspace_runtime();
    (void)maybeRestoreWorkspaceSnapshot();

    if (const auto active = active_window())
        return active;

    return CanvasLayoutInternal::pick_cross_monitor_target_window(monitor, workspaceId, direction, sourceWindow);
}

bool GridLayout::adopt_cross_monitor_window(PHLWINDOW window, PHLMONITOR monitor, bool focusWindow) {
    ensure_workspace_runtime();
    (void)maybeRestoreWorkspaceSnapshot();
    if (!manage_window(window, monitor, focusWindow))
        (void)model.focus_window(ScrollerCore::window_key(window));

    if (focusWindow)
        focus_active_window("grid_adopt_cross_monitor_window");
    return window != nullptr;
}

void GridLayout::recalculateMonitor(const int& monitorId) {
    ensure_workspace_runtime();
    const auto currentWorkspace = workspace();
    const auto monitor = currentWorkspace ? CanvasLayoutInternal::visible_monitor_for_workspace(currentWorkspace)
                                          : g_pCompositor->getMonitorFromID(monitorId);
    if (!monitor || monitor->m_id != monitorId)
        return;

    relayout(monitor);
}

void GridLayout::prepareForOverviewSnapshot() {
    ensure_workspace_runtime();
    (void)maybeRestoreWorkspaceSnapshot();
    relayout(resolve_monitor());
}

CanvasOverviewSnapshot GridLayout::buildOverviewSnapshot() const {
    CanvasOverviewSnapshot snapshot;

    const auto window = reference_window();
    if (!window)
        return snapshot;

    snapshot.workspaceId = window->workspaceID();
    const auto monitor = visible_monitor(g_pCompositor->getMonitorFromID(window->monitorID()));
    snapshot.monitorId = monitor ? monitor->m_id : window->monitorID();
    if (!monitor)
        return snapshot;

    const auto bounds = CanvasLayoutInternal::compute_canvas_bounds(monitor);
    const auto profile = current_profile(monitor);
    for (const auto& item : model.render(viewport, profile, bounds.full, bounds.max)) {
        const auto it = windowsByKey.find(item.key);
        if (it == windowsByKey.end() || !it->second)
            continue;

        snapshot.windows.push_back({
            .window = it->second,
            .box = item.logicalBox,
        });
    }

    return snapshot;
}

void GridLayout::persistCurrentSnapshot() {
    persistSnapshot();
}

bool GridLayout::handoffFocusAcrossMonitor(int workspace,
                                           Direction direction,
                                           PHLWINDOW sourceWindow,
                                           PHLMONITOR,
                                           PHLMONITOR targetMonitor) {
    if (!targetMonitor)
        return false;

    const auto workspaceId = CanvasLayoutInternal::preferred_workspace_id(targetMonitor, workspace);
    const auto targetWorkspace = g_pCompositor->getWorkspaceByID(workspaceId);
    auto* targetGrid = grid_for_workspace(workspaceId);
    auto* targetCanvas = CanvasLayoutInternal::get_canvas_for_workspace(workspaceId);
    PHLWINDOW targetWindow = nullptr;
    if (targetGrid && targetGrid != this) {
        targetWindow = targetGrid->preferred_focus_window(targetMonitor, workspaceId, direction, sourceWindow);
    }

    if (!targetWindow)
        targetWindow = CanvasLayoutInternal::pick_cross_monitor_target_window(targetMonitor, workspaceId, direction, sourceWindow);

    persistSnapshot();
    if (!targetWindow) {
        return CanvasLayoutInternal::focus_monitor_workspace(targetMonitor,
                                                             targetWorkspace,
                                                             workspaceId,
                                                             true,
                                                             "grid_move_focus_cross_monitor_empty_target");
    }

    if (targetGrid && targetGrid != this)
        targetGrid->focus_window(targetWindow);
    if (targetCanvas) {
        targetCanvas->onWindowFocusChange(targetWindow);
        targetCanvas->recalculateMonitor(targetMonitor->m_id);
    }

    return CanvasLayoutInternal::switch_to_window(targetWindow, true);
}

bool GridLayout::handoffMoveWindowAcrossMonitor(int workspace,
                                                Direction direction,
                                                PHLWINDOW currentWindow,
                                                PHLMONITOR sourceMonitor,
                                                PHLMONITOR targetMonitor) {
    (void)direction;
    if (!currentWindow || !sourceMonitor || !targetMonitor)
        return false;

    const auto workspaceId = CanvasLayoutInternal::preferred_workspace_id(targetMonitor, workspace);
    auto* targetGrid = grid_for_workspace(workspaceId);
    auto* targetCanvas = CanvasLayoutInternal::get_canvas_for_workspace(workspaceId);
    if ((!targetGrid || targetGrid == this) && !targetCanvas)
        return false;

    const auto targetWorkspace = g_pCompositor->getWorkspaceByID(workspaceId);
    const auto selector = ScrollerCore::workspace_selector(targetWorkspace);
    if (!CanvasLayoutInternal::can_invoke_dispatcher("movetoworkspacesilent", selector, "grid_move_window_cross_monitor"))
        return false;
    if (!CanvasLayoutInternal::invoke_dispatcher("movetoworkspacesilent", selector, "grid_move_window_cross_monitor"))
        return false;

    const auto key = ScrollerCore::window_key(currentWindow);
    windowsByKey.erase(key);
    model.remove_window(key);
    if (fullscreenKey && *fullscreenKey == key)
        fullscreenKey.reset();
    relayout(sourceMonitor);
    persistSnapshot();

    if (targetGrid && targetGrid != this) {
        targetGrid->adopt_cross_monitor_window(currentWindow, targetMonitor, true);
        targetGrid->focus_window(currentWindow);
    }
    if (targetCanvas) {
        (void)targetCanvas->onWindowCreatedTiling(currentWindow);
        targetCanvas->onWindowFocusChange(currentWindow);
        targetCanvas->recalculateMonitor(targetMonitor->m_id);
        targetCanvas->persistCurrentSnapshot();
    }
    return CanvasLayoutInternal::switch_to_window(currentWindow, true);
}

} // namespace ScrollerGrid
