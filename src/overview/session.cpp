/**
 * @file session.cpp
 * @brief Global overview-session construction and logical target navigation.
 *
 * This file builds a monitor-scoped preview model from the current set of
 * tiled windows, keeps a logical selection independent from Hyprland focus,
 * and resolves the final workspace/window jump only when overview closes with
 * acceptance.
 */
#include "session.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>

#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/desktop/Workspace.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <spdlog/spdlog.h>

#include "../layout/canvas/internal.h"

namespace Overview {
namespace {

using ScrollerCore::Box;

std::string workspace_selector(PHLWORKSPACE workspace) {
    if (!workspace)
        return {};

    if (!workspace->m_name.empty())
        return workspace->m_name;

    return std::to_string(workspace->m_id);
}

bool is_tiled_overview_window(PHLWINDOW window) {
    return window && window->m_isMapped && !window->m_isFloating;
}

PHLMONITOR monitor_for_workspace(PHLWORKSPACE workspace) {
    if (!workspace)
        return nullptr;

    if (const auto monitor = CanvasLayoutInternal::visible_monitor_for_workspace(workspace))
        return monitor;

    return g_pCompositor->getMonitorFromID(workspace->monitorID());
}

Box window_box(PHLWINDOW window) {
    if (!window)
        return {};

    return {window->m_position.x, window->m_position.y, window->m_size.x, window->m_size.y};
}

Box union_box(const std::vector<Target>& targets) {
    if (targets.empty())
        return {};

    auto left = targets.front().box.x;
    auto top = targets.front().box.y;
    auto right = targets.front().box.x + targets.front().box.w;
    auto bottom = targets.front().box.y + targets.front().box.h;

    for (const auto& target : targets) {
        left = std::min(left, target.box.x);
        top = std::min(top, target.box.y);
        right = std::max(right, target.box.x + target.box.w);
        bottom = std::max(bottom, target.box.y + target.box.h);
    }

    return {left, top, right - left, bottom - top};
}

void scale_workspace_targets(WorkspaceNode& node) {
    if (node.targets.empty())
        return;

    const auto sourceBounds = union_box(node.targets);
    const auto usableWidth = std::max(1.0, node.box.w - 24.0);
    const auto usableHeight = std::max(1.0, node.box.h - 24.0);
    const auto sourceWidth = std::max(1.0, sourceBounds.w);
    const auto sourceHeight = std::max(1.0, sourceBounds.h);
    const auto scale = std::min(usableWidth / sourceWidth, usableHeight / sourceHeight);
    const auto offsetX = node.box.x + (node.box.w - sourceWidth * scale) / 2.0;
    const auto offsetY = node.box.y + (node.box.h - sourceHeight * scale) / 2.0;

    for (auto& target : node.targets) {
        const auto relativeX = target.box.x - sourceBounds.x;
        const auto relativeY = target.box.y - sourceBounds.y;
        target.box = {
            offsetX + relativeX * scale,
            offsetY + relativeY * scale,
            std::max(24.0, target.box.w * scale),
            std::max(24.0, target.box.h * scale),
        };
    }
}

double center_x(const Box& box) {
    return box.x + box.w / 2.0;
}

double center_y(const Box& box) {
    return box.y + box.h / 2.0;
}

bool is_in_direction(const Box& from, const Box& candidate, Direction direction) {
    switch (direction) {
        case Direction::Left:
            return center_x(candidate) < center_x(from);
        case Direction::Right:
            return center_x(candidate) > center_x(from);
        case Direction::Up:
            return center_y(candidate) < center_y(from);
        case Direction::Down:
            return center_y(candidate) > center_y(from);
        default:
            return false;
    }
}

double primary_distance(const Box& from, const Box& candidate, Direction direction) {
    switch (direction) {
        case Direction::Left:
            return center_x(from) - center_x(candidate);
        case Direction::Right:
            return center_x(candidate) - center_x(from);
        case Direction::Up:
            return center_y(from) - center_y(candidate);
        case Direction::Down:
            return center_y(candidate) - center_y(from);
        default:
            return std::numeric_limits<double>::infinity();
    }
}

double secondary_distance(const Box& from, const Box& candidate, Direction direction) {
    switch (direction) {
        case Direction::Left:
        case Direction::Right:
            return std::abs(center_y(candidate) - center_y(from));
        case Direction::Up:
        case Direction::Down:
            return std::abs(center_x(candidate) - center_x(from));
        default:
            return std::numeric_limits<double>::infinity();
    }
}

bool same_target(const Target& a, const Target& b) {
    return a.type == b.type && a.workspaceId == b.workspaceId && a.monitorId == b.monitorId && a.window == b.window;
}

} // namespace

bool Session::active() const {
    return active_;
}

const std::vector<MonitorRegion>& Session::monitors() const {
    return monitors_;
}

const std::optional<Target>& Session::selection() const {
    return selection_;
}

void Session::damageMonitors() const {
    for (const auto& region : monitors_) {
        if (region.monitor)
            g_pHyprRenderer->damageMonitor(region.monitor);
    }
}

Session& session() {
    static Session instance;
    return instance;
}

void Session::clear() {
    active_ = false;
    originWorkspace_ = WORKSPACE_INVALID;
    originWindow_ = nullptr;
    monitors_.clear();
    selection_.reset();
    syntheticEmptyTarget_.reset();
}

WORKSPACEID Session::nextWorkspaceId() const {
    WORKSPACEID maxWorkspaceId = 0;

    for (const auto& workspaceRef : g_pCompositor->getWorkspaces()) {
        const auto workspace = workspaceRef.lock();
        if (!workspace)
            continue;

        maxWorkspaceId = std::max(maxWorkspaceId, workspace->m_id);
    }

    return maxWorkspaceId + 1;
}

const MonitorRegion* Session::regionForMonitor(int monitorId) const {
    for (const auto& region : monitors_) {
        if (region.monitorId == monitorId)
            return &region;
    }

    return nullptr;
}

std::vector<const Target*> Session::collectTargets() const {
    std::vector<const Target*> targets;

    for (const auto& monitor : monitors_) {
        for (const auto& workspace : monitor.workspaces) {
            for (const auto& target : workspace.targets)
                targets.push_back(&target);
        }
    }

    if (syntheticEmptyTarget_)
        targets.push_back(&*syntheticEmptyTarget_);

    return targets;
}

void Session::rebuild() {
    monitors_.clear();
    syntheticEmptyTarget_.reset();

    for (const auto& monitor : g_pCompositor->m_monitors) {
        if (!monitor)
            continue;

        const auto bounds = CanvasLayoutInternal::compute_canvas_bounds(monitor);
        MonitorRegion region;
        region.monitorId = monitor->m_id;
        region.monitor = monitor;
        region.box = bounds.max;
        monitors_.push_back(std::move(region));
    }

    for (auto& region : monitors_) {
        std::vector<PHLWORKSPACE> workspaces;

        for (const auto& workspaceRef : g_pCompositor->getWorkspaces()) {
            const auto workspace = workspaceRef.lock();
            if (!workspace || monitor_for_workspace(workspace) != region.monitor)
                continue;

            WorkspaceNode node;
            node.workspaceId = workspace->m_id;
            node.monitorId = region.monitorId;

            for (const auto& window : g_pCompositor->m_windows) {
                if (!is_tiled_overview_window(window) || window->workspaceID() != workspace->m_id)
                    continue;

                Target target;
                target.type = TargetType::Window;
                target.workspaceId = workspace->m_id;
                target.monitorId = region.monitorId;
                target.window = window;
                target.box = window_box(window);
                target.synthetic = false;
                node.targets.push_back(std::move(target));
            }

            if (node.targets.empty())
                continue;

            region.workspaces.push_back(std::move(node));
        }

        std::sort(region.workspaces.begin(), region.workspaces.end(), [](const WorkspaceNode& a, const WorkspaceNode& b) {
            return a.workspaceId < b.workspaceId;
        });

        const auto count = static_cast<double>(region.workspaces.size());
        if (count == 0.0)
            continue;

        const auto sliceHeight = region.box.h / count;
        auto index = 0.0;
        for (auto& workspace : region.workspaces) {
            workspace.box = {
                region.box.x,
                region.box.y + sliceHeight * index,
                region.box.w,
                sliceHeight,
            };
            scale_workspace_targets(workspace);
            index += 1.0;
        }
    }
}

bool Session::selectInitialTarget() {
    const auto targets = collectTargets();
    if (targets.empty()) {
        if (monitors_.empty())
            return false;

        const auto* region = regionForMonitor(g_pCompositor->getMonitorFromCursor() ? g_pCompositor->getMonitorFromCursor()->m_id : monitors_.front().monitorId);
        if (!region)
            region = &monitors_.front();

        const auto workspaceId = nextWorkspaceId();
        Target target;
        target.type = TargetType::EmptyWorkspace;
        target.workspaceId = workspaceId;
        target.monitorId = region->monitorId;
        target.window = nullptr;
        target.box = {region->box.x + region->box.w * 0.25, region->box.y + region->box.h * 0.25, region->box.w * 0.5, region->box.h * 0.5};
        target.synthetic = true;
        selection_ = target;
        syntheticEmptyTarget_ = selection_;
        return true;
    }

    if (originWindow_) {
        const auto it = std::find_if(targets.begin(), targets.end(), [&](const Target* target) {
            return target->window == originWindow_;
        });
        if (it != targets.end()) {
            selection_ = **it;
            return true;
        }
    }

    if (originWorkspace_ != WORKSPACE_INVALID) {
        const auto it = std::find_if(targets.begin(), targets.end(), [&](const Target* target) {
            return target->workspaceId == originWorkspace_;
        });
        if (it != targets.end()) {
            selection_ = **it;
            return true;
        }
    }

    selection_ = *targets.front();
    return true;
}

void Session::open() {
    if (active_)
        return;

    originWorkspace_ = CanvasLayoutInternal::get_workspace_id();
    if (const auto workspace = g_pCompositor->getWorkspaceByID(originWorkspace_))
        originWindow_ = workspace->getLastFocusedWindow();

    rebuild();
    if (!selectInitialTarget()) {
        clear();
        spdlog::warn("overview_open: no targets available");
        return;
    }

    active_ = true;
    damageMonitors();
    spdlog::info("overview_open: origin_workspace={} origin_window={} monitors={} selection_workspace={} selection_window={} synthetic={}",
                 originWorkspace_,
                 static_cast<const void*>(originWindow_ ? originWindow_.get() : nullptr),
                 monitors_.size(),
                 selection_ ? selection_->workspaceId : WORKSPACE_INVALID,
                 static_cast<const void*>(selection_ && selection_->window ? selection_->window.get() : nullptr),
                 selection_ ? selection_->synthetic : false);
}

const Target* Session::findBestTarget(Direction direction) const {
    if (!selection_)
        return nullptr;

    const auto targets = collectTargets();
    const auto& current = *selection_;
    const Target* bestTarget = nullptr;
    auto bestPrimary = std::numeric_limits<double>::infinity();
    auto bestMonitorPenalty = std::numeric_limits<int>::max();
    auto bestSecondary = std::numeric_limits<double>::infinity();

    for (const auto* candidate : targets) {
        if (!candidate || same_target(*candidate, current))
            continue;
        if (!is_in_direction(current.box, candidate->box, direction))
            continue;

        const auto primary = primary_distance(current.box, candidate->box, direction);
        const auto monitorPenalty = candidate->monitorId == current.monitorId ? 0 : 1;
        const auto secondary = secondary_distance(current.box, candidate->box, direction);

        if (!bestTarget || primary < bestPrimary ||
            (primary == bestPrimary && monitorPenalty < bestMonitorPenalty) ||
            (primary == bestPrimary && monitorPenalty == bestMonitorPenalty && secondary < bestSecondary)) {
            bestTarget = candidate;
            bestPrimary = primary;
            bestMonitorPenalty = monitorPenalty;
            bestSecondary = secondary;
        }
    }

    return bestTarget;
}

bool Session::createSyntheticEmptyTarget(Direction direction) {
    if (!selection_)
        return false;

    const auto* region = regionForMonitor(selection_->monitorId);
    if (!region)
        return false;

    auto box = selection_->box;
    const auto stepX = std::max(box.w, region->box.w * 0.35);
    const auto stepY = std::max(box.h, region->box.h * 0.35);
    switch (direction) {
        case Direction::Left:
            box.x -= stepX;
            break;
        case Direction::Right:
            box.x += stepX;
            break;
        case Direction::Up:
            box.y -= stepY;
            break;
        case Direction::Down:
            box.y += stepY;
            break;
        default:
            return false;
    }

    box.w = std::min(box.w, region->box.w);
    box.h = std::min(box.h, region->box.h);
    box.x = std::clamp(box.x, region->box.x, region->box.x + std::max(0.0, region->box.w - box.w));
    box.y = std::clamp(box.y, region->box.y, region->box.y + std::max(0.0, region->box.h - box.h));

    Target target;
    target.type = TargetType::EmptyWorkspace;
    target.workspaceId = nextWorkspaceId();
    target.monitorId = region->monitorId;
    target.window = nullptr;
    target.box = box;
    target.synthetic = true;
    syntheticEmptyTarget_ = target;
    selection_ = syntheticEmptyTarget_;
    spdlog::info("overview_create_empty: workspace={} monitor={} box=({}, {}, {}, {})",
                 syntheticEmptyTarget_->workspaceId,
                 syntheticEmptyTarget_->monitorId,
                 syntheticEmptyTarget_->box.x,
                 syntheticEmptyTarget_->box.y,
                 syntheticEmptyTarget_->box.w,
                 syntheticEmptyTarget_->box.h);
    return true;
}

bool Session::moveSelection(Direction direction) {
    if (!active_ || !selection_)
        return false;

    if (const auto* target = findBestTarget(direction)) {
        selection_ = *target;
        if (!selection_->synthetic)
            syntheticEmptyTarget_.reset();
        spdlog::info("overview_move: direction={} workspace={} window={} synthetic={}",
                     ScrollerCore::direction_name(direction),
                     selection_->workspaceId,
                     static_cast<const void*>(selection_->window ? selection_->window.get() : nullptr),
                     selection_->synthetic);
        damageMonitors();
        return true;
    }

    const auto created = createSyntheticEmptyTarget(direction);
    if (created)
        damageMonitors();
    return created;
}

void Session::acceptSelection() {
    if (!selection_)
        return;

    if (selection_->type == TargetType::EmptyWorkspace) {
        CanvasLayoutInternal::invoke_dispatcher("workspace", std::to_string(selection_->workspaceId), "overview_accept_empty");
        spdlog::info("overview_accept_empty: workspace={} monitor={}", selection_->workspaceId, selection_->monitorId);
        return;
    }

    const auto workspace = g_pCompositor->getWorkspaceByID(selection_->workspaceId);
    if (!workspace || !selection_->window) {
        spdlog::warn("overview_accept_window: invalid target workspace={} window={}",
                     selection_->workspaceId,
                     static_cast<const void*>(selection_->window ? selection_->window.get() : nullptr));
        return;
    }

    if (workspace->m_isSpecialWorkspace)
        CanvasLayoutInternal::invoke_dispatcher("togglespecialworkspace", workspace_selector(workspace), "overview_accept_special");
    else
        CanvasLayoutInternal::invoke_dispatcher("workspace", workspace_selector(workspace), "overview_accept_window");

    CanvasLayoutInternal::switch_to_window(selection_->window, true);
    spdlog::info("overview_accept_window: workspace={} window={} special={}",
                 selection_->workspaceId,
                 static_cast<const void*>(selection_->window.get()),
                 workspace->m_isSpecialWorkspace);
}

void Session::close(bool acceptSelectionFlag) {
    if (!active_)
        return;

    if (acceptSelectionFlag)
        acceptSelection();

    spdlog::info("overview_close: accepted={} selection_workspace={} selection_window={}",
                 acceptSelectionFlag,
                 selection_ ? selection_->workspaceId : WORKSPACE_INVALID,
                 static_cast<const void*>(selection_ && selection_->window ? selection_->window.get() : nullptr));
    damageMonitors();
    clear();
}

} // namespace Overview
