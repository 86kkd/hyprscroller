//#define COLORS_IPC

#include <cmath>
#include <cstdio>
#include <limits>
#include <optional>

#include <hyprland/src/config/ConfigManager.hpp>
#include <hyprland/src/config/ConfigValue.hpp>
#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprland/src/layout/algorithm/Algorithm.hpp>
#include <hyprland/src/layout/space/Space.hpp>
#include <hyprland/src/helpers/Monitor.hpp>
#include <hyprland/src/managers/KeybindManager.hpp>
#include <hyprland/src/managers/input/InputManager.hpp>
#include <hyprland/src/plugins/PluginAPI.hpp>
#include <hyprland/src/desktop/Workspace.hpp>
#include <spdlog/spdlog.h>

#include "../core/core.h"
#include "row.h"
#include "scroller_layout.h"

using namespace ScrollerCore;

extern HANDLE PHANDLE;

// Global mark storage lives in this layout module.
static Marks marks;

static void switch_to_window(PHLWINDOW window, bool warp_cursor = false);

static ListNode<Row*>* find_row_node(List<Row*>& rows, Row* target) {
    for (auto row = rows.first(); row != nullptr; row = row->next()) {
        if (row->data() == target)
            return row;
    }
    return nullptr;
}

static void recalculate_workspace_row(Row* row, PHLMONITOR monitor, PHLWORKSPACE workspace, bool honor_fullscreen) {
    if (!row || !monitor || !workspace)
        return;

    row->update_sizes(monitor);
    if (honor_fullscreen && workspace->m_hasFullscreenWindow && workspace->m_fullscreenMode == FSMODE_FULLSCREEN) {
        row->set_fullscreen_active_window();
        return;
    }

    row->recalculate_row_geometry();
}

static const char* direction_name(Direction direction) {
    switch (direction) {
        case Direction::Left: return "left";
        case Direction::Right: return "right";
        case Direction::Up: return "up";
        case Direction::Down: return "down";
        case Direction::Begin: return "begin";
        case Direction::End: return "end";
        case Direction::Center: return "center";
        default: return "unknown";
    }
}

static WORKSPACEID preferred_workspace_id(PHLMONITOR monitor) {
    if (!monitor)
        return WORKSPACE_INVALID;

    const auto special_workspace_id = monitor->activeSpecialWorkspaceID();
    if (g_pCompositor->getWorkspaceByID(special_workspace_id))
        return special_workspace_id;

    return monitor->activeWorkspaceID();
}

static ScrollerLayout* get_scroller_for_workspace(const WORKSPACEID workspace_id) {
    const auto workspace = g_pCompositor->getWorkspaceByID(workspace_id);
    if (!workspace || !workspace->m_space)
        return nullptr;

    const auto algorithm = workspace->m_space->algorithm();
    if (!algorithm)
        return nullptr;

    const auto& tiled = algorithm->tiledAlgo();
    if (!tiled)
        return nullptr;

    return dynamic_cast<ScrollerLayout*>(tiled.get());
}

static std::optional<Math::eDirection> direction_to_math(Direction direction) {
    switch (direction) {
        case Direction::Left:
            return Math::fromChar('l');
        case Direction::Right:
            return Math::fromChar('r');
        case Direction::Up:
            return Math::fromChar('u');
        case Direction::Down:
            return Math::fromChar('d');
        default:
            return std::nullopt;
    }
}

static double primary_cross_monitor_score(PHLWINDOW window, PHLMONITOR monitor, Direction direction) {
    const auto window_left = window->m_position.x;
    const auto window_right = window->m_position.x + window->m_size.x;
    const auto window_top = window->m_position.y;
    const auto window_bottom = window->m_position.y + window->m_size.y;
    const auto monitor_left = monitor->m_position.x;
    const auto monitor_right = monitor->m_position.x + monitor->m_size.x;
    const auto monitor_top = monitor->m_position.y;
    const auto monitor_bottom = monitor->m_position.y + monitor->m_size.y;

    switch (direction) {
        case Direction::Left:
            return std::abs(window_right - monitor_right);
        case Direction::Right:
            return std::abs(window_left - monitor_left);
        case Direction::Up:
            return std::abs(window_bottom - monitor_bottom);
        case Direction::Down:
            return std::abs(window_top - monitor_top);
        default:
            return std::numeric_limits<double>::infinity();
    }
}

static double secondary_cross_monitor_score(PHLWINDOW window, PHLWINDOW source_window, Direction direction) {
    if (!source_window)
        return 0.0;

    const auto window_middle = window->middle();
    const auto source_middle = source_window->middle();
    switch (direction) {
        case Direction::Left:
        case Direction::Right:
            return std::abs(window_middle.y - source_middle.y);
        case Direction::Up:
        case Direction::Down:
            return std::abs(window_middle.x - source_middle.x);
        default:
            return 0.0;
    }
}

static PHLWINDOW pick_cross_monitor_target_window(PHLMONITOR monitor, WORKSPACEID workspace_id, Direction direction, PHLWINDOW source_window) {
    PHLWINDOW best = nullptr;
    auto best_primary = std::numeric_limits<double>::infinity();
    auto best_secondary = std::numeric_limits<double>::infinity();

    for (const auto& window : g_pCompositor->m_windows) {
        if (!window || window->workspaceID() != workspace_id || window->m_isFloating || !window->m_isMapped || window->isHidden())
            continue;

        const auto primary = primary_cross_monitor_score(window, monitor, direction);
        const auto secondary = secondary_cross_monitor_score(window, source_window, direction);
        if (!best || primary < best_primary || (primary == best_primary && secondary < best_secondary)) {
            best = window;
            best_primary = primary;
            best_secondary = secondary;
        }
    }

    return best;
}

Row *ScrollerLayout::getRowForWorkspace(int workspace) {
    // Linear lookup because row count is typically small (per workspace list).
    for (auto row = rows.first(); row != nullptr; row = row->next()) {
        if (row->data()->get_workspace() == workspace)
            return row->data();
    }
    return nullptr;
}

Row *ScrollerLayout::getRowForWindow(PHLWINDOW window) {
    // Walk rows and let columns report membership by pointer equality.
    for (auto row = rows.first(); row != nullptr; row = row->next()) {
        if (row->data()->has_window(window))
            return row->data();
    }
    return nullptr;
}

void ScrollerLayout::newTarget(SP<Layout::ITarget> target) {
    // Called by CLayout::addTarget; add every new tiled target using default placement.
    if (!target)
        return;

    auto window = target->window();
    if (!window)
        return;

    spdlog::info("newTarget: window={} workspace={}", static_cast<const void*>(window.get()), window->workspaceID());
    onWindowCreatedTiling(window, Math::DIRECTION_DEFAULT);
    switch_to_window(window);
}

void ScrollerLayout::movedTarget(SP<Layout::ITarget> target, std::optional<Vector2D>)
{
    // Re-use create path to keep placement logic centralized.
    if (!target)
        return;

    auto window = target->window();
    if (!window)
        return;

    onWindowCreatedTiling(window, Math::DIRECTION_DEFAULT);
}

void ScrollerLayout::removeTarget(SP<Layout::ITarget> target)
{
    // Remove target from in-memory row/column model on unmap/close.
    if (!target)
        return;

    onWindowRemovedTiling(target->window());
}

void ScrollerLayout::resizeTarget(const Vector2D &delta, SP<Layout::ITarget> target, Layout::eRectCorner)
{
    // If the window is not managed by a row, resize the real animated size directly.
    auto window = windowFromTarget(target);
    if (!window)
        return;

    auto s = getRowForWindow(window);
    if (s == nullptr) {
        if (window->m_realSize)
            *window->m_realSize = Vector2D(std::max((window->m_realSize->goal() + delta).x, 20.0), std::max((window->m_realSize->goal() + delta).y, 20.0));
        window->updateWindowDecos();
        return;
    }

    s->focus_window(window);
    s->resize_active_window(delta);
}

void ScrollerLayout::recalculate()
{
    // Full layout pass: refresh every row against its current monitor and fullscreen state.
    for (auto row = rows.first(); row != nullptr; row = row->next()) {
        const auto workspace = g_pCompositor->getWorkspaceByID(row->data()->get_workspace());
        if (!workspace)
            continue;
        const auto monitor = g_pCompositor->getMonitorFromID(workspace->monitorID());
        if (!monitor)
            continue;

        row->data()->update_sizes(monitor);
        if (workspace->m_hasFullscreenWindow && workspace->m_fullscreenMode == FSMODE_FULLSCREEN)
            row->data()->set_fullscreen_active_window();
        else
            row->data()->recalculate_row_geometry();
    }
}

std::expected<void, std::string> ScrollerLayout::layoutMsg(const std::string_view&)
{
    // No custom layout message channel is implemented yet.
    return {};
}

std::optional<Vector2D> ScrollerLayout::predictSizeForNewTarget()
{
    // Predicts geometry for new tiled window creation on active monitor workspace.
    auto monitor = monitorFromPointingOrCursor();
    if (!monitor)
        return {};

    auto row = getRowForWorkspace(monitor->activeWorkspaceID());
    if (!row)
        return Vector2D(monitor->m_size.x, monitor->m_size.y);

    return row->predict_window_size();
}

SP<Layout::ITarget> ScrollerLayout::getNextCandidate(SP<Layout::ITarget> old)
{
    // Keeps cycling behavior stable by returning the current active target when possible.
    int workspace_id = WORKSPACE_INVALID;
    if (auto oldWindow = windowFromTarget(old))
        workspace_id = oldWindow->workspaceID();
    if (workspace_id == WORKSPACE_INVALID) {
        auto monitor = monitorFromPointingOrCursor();
        if (monitor)
            workspace_id = monitor->activeWorkspaceID();
    }

    auto s = getRowForWorkspace(workspace_id);
    if (!s)
        return {};

    const auto active = s->get_active_window();
    if (!active)
        return {};

    return active->layoutTarget();
}

void ScrollerLayout::swapTargets(SP<Layout::ITarget> a, SP<Layout::ITarget> b)
{
    // Only swap within one row; no cross-row move is supported for this layout.
    auto wa = windowFromTarget(a);
    auto wb = windowFromTarget(b);
    auto sa = getRowForWindow(wa);
    auto sb = getRowForWindow(wb);
    if (!wa || !wb || !sa || !sb || sa != sb)
        return;

    sa->swapWindows(wa, wb);
}

void ScrollerLayout::moveTargetInDirection(SP<Layout::ITarget> t, Math::eDirection direction, bool)
{
    // Map compositor direction into dispatcher-level focus+move behavior.
    auto window = windowFromTarget(t);
    auto s = getRowForWindow(window);
    if (!s || !window)
        return;

    s->focus_window(window);
    switch (direction) {
        case Math::DIRECTION_LEFT:
            s->move_active_column(Direction::Left);
            break;
        case Math::DIRECTION_RIGHT:
            s->move_active_column(Direction::Right);
            break;
        case Math::DIRECTION_UP:
            s->move_active_column(Direction::Up);
            break;
        case Math::DIRECTION_DOWN:
            s->move_active_column(Direction::Down);
            break;
        default:
            return;
    }
}

void ScrollerLayout::onWindowCreatedTiling(PHLWINDOW window, Math::eDirection)
{
    auto s = getRowForWorkspace(window->workspaceID());
    if (s == nullptr) {
        s = new Row(window);
        rows.push_back(s);
    }
    s->add_active_window(window);
}

void ScrollerLayout::onWindowRemovedTiling(PHLWINDOW window)
{
    const auto windowPtr = static_cast<const void*>(window.get());
    const auto workspace = window ? window->workspaceID() : WORKSPACE_INVALID;
    spdlog::info("onWindowRemovedTiling: window={} workspace={}", windowPtr, workspace);

    marks.remove(window);

    auto s = getRowForWindow(window);
    if (s == nullptr) {
        spdlog::debug("onWindowRemovedTiling: no row found for window={} workspace={}", windowPtr, workspace);
        return;
    }

    if (s->remove_window(window))
        return;

    auto row = find_row_node(rows, s);
    if (!row) {
        spdlog::warn("onWindowRemovedTiling: empty row missing from list row={} workspace={}",
                     static_cast<const void*>(s), workspace);
        return;
    }

    auto doomed = row->data();
    spdlog::info("onWindowRemovedTiling: deleting empty row={} workspace={}",
                 static_cast<const void*>(doomed), doomed->get_workspace());
    rows.erase(row);
    delete doomed;
}

void ScrollerLayout::onWindowFocusChange(PHLWINDOW window)
{
    // Update active row/column when focus changes.
    if (window == nullptr) {
        return;
    }

    auto s = getRowForWindow(window);
    if (s == nullptr) {
        return;
    }
    s->focus_window(window);
}

bool ScrollerLayout::isWindowTiled(PHLWINDOW window)
{
    return getRowForWindow(window) != nullptr;
}

void ScrollerLayout::recalculateMonitor(const int &monitor_id)
{
    auto PMONITOR = g_pCompositor->getMonitorFromID(monitor_id);
    if (!PMONITOR)
        return;

    g_pHyprRenderer->damageMonitor(PMONITOR);

    auto PWORKSPACE = PMONITOR->m_activeWorkspace;
    if (!PWORKSPACE)
        return;

    recalculate_workspace_row(getRowForWorkspace(PWORKSPACE->m_id), PMONITOR, PWORKSPACE, true);

    const auto special_workspace_id = PMONITOR->activeSpecialWorkspaceID();
    const auto special_workspace = g_pCompositor->getWorkspaceByID(special_workspace_id);
    spdlog::debug("recalculateMonitor: monitor={} active_ws={} special_ws={} special_exists={}",
                  monitor_id, PWORKSPACE->m_id, special_workspace_id, special_workspace != nullptr);

    recalculate_workspace_row(getRowForWorkspace(special_workspace_id), PMONITOR, special_workspace, false);
}

void ScrollerLayout::recalculateWindow(PHLWINDOW window)
{
    auto s = getRowForWindow(window);
    if (s == nullptr)
        return;

    s->recalculate_row_geometry();
}

void ScrollerLayout::resizeActiveWindow(PHLWINDOW window, const Vector2D &delta,
                                        Layout::eRectCorner, PHLWINDOW pWindow)
{
    const auto PWINDOW = pWindow ? pWindow : window;
    if (!PWINDOW)
        return;

    auto s = getRowForWindow(PWINDOW);
    if (s == nullptr) {
        // Window is not tiled
        if (PWINDOW->m_realSize)
            *PWINDOW->m_realSize = Vector2D(std::max((PWINDOW->m_realSize->goal() + delta).x, 20.0), std::max((PWINDOW->m_realSize->goal() + delta).y, 20.0));
        PWINDOW->updateWindowDecos();
        return;
    }

    s->resize_active_window(delta);
}

void ScrollerLayout::moveWindowTo(PHLWINDOW window, const std::string &direction, bool)
{
    auto s = getRowForWindow(window);
    if (s == nullptr) {
        return;
    } else if (!(s->is_active(window))) {
        // cannot move non active window?
        return;
    }

    switch (direction.at(0)) {
        case 'l': s->move_active_column(Direction::Left); break;
        case 'r': s->move_active_column(Direction::Right); break;
        case 'u': s->move_active_column(Direction::Up); break;
        case 'd': s->move_active_column(Direction::Down); break;
        default: break;
    }
}

void ScrollerLayout::alterSplitRatio(PHLWINDOW, float, bool)
{
    // Compatibility hook for split ratio changes; not used for this layout.
}

void ScrollerLayout::onEnable() {
    marks.reset();
    spdlog::info("onEnable: rebuilding scroller rows from mapped windows");
    for (auto& window : g_pCompositor->m_windows) {
        spdlog::debug("onEnable: candidate window={} workspace={} mapped={} hidden={} floating={}",
                      static_cast<const void*>(window.get()), window->workspaceID(), window->m_isMapped,
                      window->isHidden(), window->m_isFloating);
        if (window->m_isFloating || !window->m_isMapped)
            continue;

        spdlog::info("onEnable: registering window={} workspace={} hidden={}",
                     static_cast<const void*>(window.get()), window->workspaceID(), window->isHidden());
        onWindowCreatedTiling(window);
        recalculateMonitor(window->monitorID());
    }
}

void ScrollerLayout::onDisable() {
    for (auto row = rows.first(); row != nullptr; row = row->next()) {
        delete row->data();
    }
    rows.clear();
    marks.reset();
}

Vector2D ScrollerLayout::predictSizeForNewWindowTiled() {
    auto monitor = monitorFromPointingOrCursor();
    if (!monitor)
        return {};

    int workspace_id = monitor->activeWorkspaceID();
    auto s = getRowForWorkspace(workspace_id);
    if (s == nullptr) {
        return monitor->m_size;
    }

    return s->predict_window_size();
}

void ScrollerLayout::cycle_window_size(int workspace, int step)
{
    auto s = getRowForWorkspace(workspace);
    if (s == nullptr) {
        return;
    }

    s->resize_active_column(step);
}

// Focus a window and force mouse/focus state sync so pointer-driven workflows work.
static void switch_to_window(PHLWINDOW window, bool warp_cursor)
{
    if (!window)
        return;

    if (!g_pCompositor->isWindowActive(window)) {
        spdlog::debug("switch_to_window: focusing window={} workspace={}",
                      static_cast<const void*>(window.get()), window->workspaceID());
        char selector[64];
        std::snprintf(selector, sizeof(selector), "address:0x%lx",
                      reinterpret_cast<unsigned long>(window.get()));
        g_pKeybindManager->m_dispatchers["focuswindow"](selector);
    }

    if (warp_cursor)
        window->warpCursor(true);
}

void ScrollerLayout::move_focus(int workspace, Direction direction)
{
    const auto focus_move_result_name = [](FocusMoveResult result) {
        switch (result) {
        case FocusMoveResult::Moved:
            return "moved";
        case FocusMoveResult::NoOp:
            return "noop";
        case FocusMoveResult::CrossMonitor:
            return "cross_monitor";
        }

        return "unknown";
    };

    static auto* const *focus_wrap = (Hyprlang::INT* const *)HyprlandAPI::getConfigValue(PHANDLE, "plugin:scroller:focus_wrap")->getDataStaticPtr();
    auto s = getRowForWorkspace(workspace);
    const auto before = s ? s->get_active_window() : nullptr;
    const auto beforeMonitor = before ? g_pCompositor->getMonitorFromID(before->monitorID()) : monitorFromPointingOrCursor();
    const auto beforeActiveWorkspaceId = beforeMonitor ? beforeMonitor->activeWorkspaceID() : WORKSPACE_INVALID;
    const auto beforeSpecialWorkspaceId = beforeMonitor ? beforeMonitor->activeSpecialWorkspaceID() : WORKSPACE_INVALID;
    spdlog::info("move_focus: workspace={} direction={} row_found={} before={}",
                 workspace, direction_name(direction), s != nullptr, static_cast<const void*>(before ? before.get() : nullptr));
    if (s == nullptr) {
        // if workspace is empty, use the deault movefocus, which now
        // is "move to another monitor" (pass the direction)
        switch (direction) {
            case Direction::Left:
                g_pKeybindManager->m_dispatchers["movefocus"]("l");
                break;
            case Direction::Right:
                g_pKeybindManager->m_dispatchers["movefocus"]("r");
                break;
            case Direction::Up:
                g_pKeybindManager->m_dispatchers["movefocus"]("u");
                break;
            case Direction::Down:
                g_pKeybindManager->m_dispatchers["movefocus"]("d");
                break;
            default:
                break;
        }
        return;
    }

    const auto moveResult = s->move_focus(direction, **focus_wrap == 0 ? false : true);
    if (moveResult == FocusMoveResult::CrossMonitor) {
        const auto monitorDirection = direction_to_math(direction);
        auto monitor = beforeMonitor && monitorDirection ? g_pCompositor->getMonitorInDirection(beforeMonitor, *monitorDirection) : nullptr;
        const auto activeWorkspaceId = monitor ? monitor->activeWorkspaceID() : WORKSPACE_INVALID;
        const auto workspaceId = preferred_workspace_id(monitor);
        const auto specialWorkspaceId = monitor ? monitor->activeSpecialWorkspaceID() : WORKSPACE_INVALID;
        spdlog::info(
            "move_focus_cross_monitor: source_workspace={} direction={} source_window={} "
            "source_active_ws={} source_special_ws={} dest_active_ws={} dest_special_ws={} selected_ws={}",
            workspace,
            direction_name(direction),
            static_cast<const void*>(before ? before.get() : nullptr),
            beforeActiveWorkspaceId,
            beforeSpecialWorkspaceId,
            activeWorkspaceId,
            specialWorkspaceId,
            workspaceId);
        if (!monitor) {
            spdlog::warn("move_focus: no monitor in direction={} from workspace={}", direction_name(direction), workspace);
            return;
        }

        const char* targetSelection = "geometry";
        auto targetLayout = get_scroller_for_workspace(workspaceId);
        auto targetRow = targetLayout ? targetLayout->getRowForWorkspace(workspaceId) : nullptr;
        auto crossMonitorTarget = targetRow ? targetRow->get_active_window() : nullptr;
        if (crossMonitorTarget)
            targetSelection = "active";
        else
            crossMonitorTarget = pick_cross_monitor_target_window(monitor, workspaceId, direction, before);
        if (!crossMonitorTarget) {
            spdlog::warn("move_focus: no target window for crossed monitor workspace={}", workspaceId);
            return;
        }

        if (!targetRow || !targetRow->has_window(crossMonitorTarget))
            targetRow = targetLayout ? targetLayout->getRowForWindow(crossMonitorTarget) : nullptr;
        spdlog::info(
            "move_focus_cross_monitor_target: selected_ws={} target_window={} target_workspace={} "
            "target_layout_found={} target_row_found={} selection={} target_pos=({}, {}) target_size=({}, {})",
            workspaceId,
            static_cast<const void*>(crossMonitorTarget ? crossMonitorTarget.get() : nullptr),
            crossMonitorTarget ? crossMonitorTarget->workspaceID() : WORKSPACE_INVALID,
            targetLayout != nullptr,
            targetRow != nullptr,
            targetSelection,
            crossMonitorTarget ? crossMonitorTarget->m_position.x : 0.0,
            crossMonitorTarget ? crossMonitorTarget->m_position.y : 0.0,
            crossMonitorTarget ? crossMonitorTarget->m_size.x : 0.0,
            crossMonitorTarget ? crossMonitorTarget->m_size.y : 0.0);

        if (targetRow != nullptr)
            targetRow->focus_window(crossMonitorTarget);
        else
            spdlog::warn("move_focus: no row for crossed monitor target window={} workspace={}",
                         static_cast<const void*>(crossMonitorTarget.get()), workspaceId);

        spdlog::info("move_focus: workspace={} direction={} after={} result={}",
                     workspace,
                     direction_name(direction),
                     static_cast<const void*>(crossMonitorTarget.get()),
                     focus_move_result_name(moveResult));
        switch_to_window(crossMonitorTarget, true);
        return;
    }

    const auto after = s->get_active_window();
    spdlog::info("move_focus: workspace={} direction={} after={} result={}",
                 workspace, direction_name(direction), static_cast<const void*>(after ? after.get() : nullptr),
                 focus_move_result_name(moveResult));
    if (moveResult == FocusMoveResult::NoOp)
        return;
    switch_to_window(s->get_active_window(), true);
}

void ScrollerLayout::replaceWindowDataWith(PHLWINDOW, PHLWINDOW)
{
    // Compatibility hook kept for API symmetry; no metadata replacement needed.
}

void ScrollerLayout::move_window(int workspace, Direction direction) {
    auto s = getRowForWorkspace(workspace);
    if (s == nullptr) {
        return;
    }

    s->move_active_column(direction);
    switch_to_window(s->get_active_window());
}

void ScrollerLayout::align_window(int workspace, Direction direction) {
    auto s = getRowForWorkspace(workspace);
    if (s == nullptr) {
        return;
    }

    s->align_column(direction);
}

void ScrollerLayout::admit_window_left(int workspace) {
    auto s = getRowForWorkspace(workspace);
    if (s == nullptr) {
        return;
    }
    s->admit_window_left();
}

void ScrollerLayout::expel_window_right(int workspace) {
    auto s = getRowForWorkspace(workspace);
    if (s == nullptr) {
        return;
    }
    s->expel_window_right();
}

void ScrollerLayout::set_mode(int workspace, Mode mode) {
    auto s = getRowForWorkspace(workspace);
    if (s == nullptr) {
        return;
    }
    s->set_mode(mode);
}

void ScrollerLayout::fit_size(int workspace, FitSize fitsize) {
    auto s = getRowForWorkspace(workspace);
    if (s == nullptr) {
        return;
    }
    s->fit_size(fitsize);
}

void ScrollerLayout::toggle_overview(int workspace) {
    auto s = getRowForWorkspace(workspace);
    if (s == nullptr) {
        return;
    }
    s->toggle_overview();
}

void ScrollerLayout::toggle_fullscreen(int workspace) {
    auto s = getRowForWorkspace(workspace);
    if (s == nullptr) {
        return;
    }
    s->toggle_fullscreen_active_window();
}

static int get_workspace_id() {
    int workspace_id;
    auto monitor = monitorFromPointingOrCursor();
    if (!monitor)
        return -1;

    workspace_id = preferred_workspace_id(monitor);
    if (workspace_id == WORKSPACE_INVALID)
        return -1;
    if (g_pCompositor->getWorkspaceByID(workspace_id) == nullptr)
        return -1;

    return workspace_id;
}

void ScrollerLayout::marks_add(const std::string &name) {
    auto workspace = getRowForWorkspace(get_workspace_id());
    if (!workspace)
        return;

    PHLWINDOW w = workspace->get_active_window();
    if (!w)
        return;

    marks.add(w, name);
}

void ScrollerLayout::marks_delete(const std::string &name) {
    marks.del(name);
}

void ScrollerLayout::marks_visit(const std::string &name) {
    PHLWINDOW window = marks.visit(name);
    if (window != nullptr)
        switch_to_window(window);
}

void ScrollerLayout::marks_reset() {
    marks.reset();
}
