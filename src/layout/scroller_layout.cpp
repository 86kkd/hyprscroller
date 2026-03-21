#include <algorithm>

#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/helpers/Monitor.hpp>
#include <hyprland/src/layout/algorithm/Algorithm.hpp>
#include <hyprland/src/layout/space/Space.hpp>
#include <spdlog/spdlog.h>

#include "../core/core.h"
#include "row.h"
#include "scroller_layout.h"
#include "scroller_layout_internal.h"

using namespace ScrollerCore;

static Marks marks;

namespace {
ListNode<Row*>* find_row_node(List<Row*>& rows, Row* target) {
    for (auto row = rows.first(); row != nullptr; row = row->next()) {
        if (row->data() == target)
            return row;
    }
    return nullptr;
}

void clear_rows(List<Row*>& rows) {
    for (auto row = rows.first(); row != nullptr; row = row->next())
        delete row->data();
    rows.clear();
}
} // namespace

Row *ScrollerLayout::getRowForWorkspace(int workspace) {
    for (auto row = rows.first(); row != nullptr; row = row->next()) {
        if (row->data()->get_workspace() == workspace)
            return row->data();
    }
    return nullptr;
}

Row *ScrollerLayout::getRowForWindow(PHLWINDOW window) {
    for (auto row = rows.first(); row != nullptr; row = row->next()) {
        if (row->data()->has_window(window))
            return row->data();
    }
    return nullptr;
}

void ScrollerLayout::newTarget(SP<Layout::ITarget> target) {
    if (!target)
        return;

    auto window = target->window();
    if (!window)
        return;

    spdlog::info("newTarget: window={} workspace={}", static_cast<const void*>(window.get()), window->workspaceID());
    onWindowCreatedTiling(window, Math::DIRECTION_DEFAULT);
    ScrollerLayoutInternal::switch_to_window(window);
}

void ScrollerLayout::movedTarget(SP<Layout::ITarget> target, std::optional<Vector2D>)
{
    if (!target)
        return;

    auto window = target->window();
    if (!window)
        return;

    onWindowCreatedTiling(window, Math::DIRECTION_DEFAULT);
}

void ScrollerLayout::removeTarget(SP<Layout::ITarget> target)
{
    if (!target)
        return;

    onWindowRemovedTiling(target->window());
}

void ScrollerLayout::resizeTarget(const Vector2D &delta, SP<Layout::ITarget> target, Layout::eRectCorner)
{
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
    for (auto row = rows.first(); row != nullptr; row = row->next()) {
        const auto workspace = g_pCompositor->getWorkspaceByID(row->data()->get_workspace());
        if (!workspace)
            continue;

        const auto monitor = ScrollerLayoutInternal::visible_monitor_for_workspace(workspace);
        if (!monitor)
            continue;

        ScrollerLayoutInternal::recalculate_workspace_row(row->data(), monitor, workspace, true);
    }
}

std::expected<void, std::string> ScrollerLayout::layoutMsg(const std::string_view&)
{
    return {};
}

std::optional<Vector2D> ScrollerLayout::predictSizeForNewTarget()
{
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

bool ScrollerLayout::isWindowTiled(PHLWINDOW window)
{
    return getRowForWindow(window) != nullptr;
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
        if (PWINDOW->m_realSize)
            *PWINDOW->m_realSize = Vector2D(std::max((PWINDOW->m_realSize->goal() + delta).x, 20.0), std::max((PWINDOW->m_realSize->goal() + delta).y, 20.0));
        PWINDOW->updateWindowDecos();
        return;
    }

    s->resize_active_window(delta);
}

void ScrollerLayout::alterSplitRatio(PHLWINDOW, float, bool)
{
}

void ScrollerLayout::onEnable() {
    clear_rows(rows);
    marks.reset();

    const auto algorithm = m_parent.lock();
    const auto space = algorithm ? algorithm->space() : nullptr;
    const auto workspace = space ? space->workspace() : nullptr;
    if (!workspace) {
        spdlog::warn("onEnable: missing parent workspace for layout instance={}",
                     static_cast<const void*>(this));
        return;
    }

    spdlog::info("onEnable: rebuilding layout instance={} workspace={} from mapped windows",
                 static_cast<const void*>(this), workspace->m_id);
    for (auto& window : g_pCompositor->m_windows) {
        spdlog::debug("onEnable: candidate instance={} window={} workspace={} mapped={} hidden={} floating={}",
                      static_cast<const void*>(this),
                      static_cast<const void*>(window.get()), window->workspaceID(), window->m_isMapped,
                      window->isHidden(), window->m_isFloating);
        if (window->workspaceID() != workspace->m_id || window->m_isFloating || !window->m_isMapped)
            continue;

        spdlog::info("onEnable: registering instance={} window={} workspace={} hidden={}",
                     static_cast<const void*>(this), static_cast<const void*>(window.get()),
                     window->workspaceID(), window->isHidden());
        onWindowCreatedTiling(window);
    }

    const auto monitor = ScrollerLayoutInternal::visible_monitor_for_workspace(workspace);
    if (!monitor) {
        spdlog::debug("onEnable: no visible monitor for instance={} workspace={}",
                      static_cast<const void*>(this), workspace->m_id);
        return;
    }

    ScrollerLayoutInternal::recalculate_workspace_row(getRowForWorkspace(workspace->m_id), monitor, workspace, !workspace->m_isSpecialWorkspace);
}

void ScrollerLayout::onDisable() {
    clear_rows(rows);
    marks.reset();
}

Vector2D ScrollerLayout::predictSizeForNewWindowTiled() {
    auto monitor = monitorFromPointingOrCursor();
    if (!monitor)
        return {};

    int workspace_id = monitor->activeWorkspaceID();
    auto s = getRowForWorkspace(workspace_id);
    if (s == nullptr)
        return monitor->m_size;

    return s->predict_window_size();
}

void ScrollerLayout::cycle_window_size(int workspace, int step)
{
    auto s = getRowForWorkspace(workspace);
    if (s == nullptr)
        return;

    s->resize_active_column(step);
}

void ScrollerLayout::replaceWindowDataWith(PHLWINDOW, PHLWINDOW)
{
}

void ScrollerLayout::marks_add(const std::string &name) {
    auto workspace = getRowForWorkspace(ScrollerLayoutInternal::get_workspace_id());
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
        ScrollerLayoutInternal::switch_to_window(window);
}

void ScrollerLayout::marks_reset() {
    marks.reset();
}
