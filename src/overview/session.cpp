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
#include "logic.h"

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

void prepareAllCanvasesForOverview() {
    for (const auto& workspaceRef : g_pCompositor->getWorkspaces()) {
        const auto workspace = workspaceRef.lock();
        if (!workspace)
            continue;

        auto* layout = CanvasLayoutInternal::get_canvas_for_workspace(workspace->m_id);
        if (!layout)
            continue;

        layout->prepareForOverviewSnapshot();
    }
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

    for (const auto& workspaceRef : g_pCompositor->getWorkspaces()) {
        const auto workspace = workspaceRef.lock();
        if (!workspace)
            continue;

        auto* layout = CanvasLayoutInternal::get_canvas_for_workspace(workspace->m_id);
        if (!layout)
            continue;

        const auto snapshot = layout->buildOverviewSnapshot();
        if (snapshot.windows.empty())
            continue;

        auto regionIt = std::find_if(monitors_.begin(), monitors_.end(), [&](const MonitorRegion& region) {
            return region.monitorId == snapshot.monitorId;
        });
        if (regionIt == monitors_.end()) {
            if (const auto monitor = monitor_for_workspace(workspace)) {
                regionIt = std::find_if(monitors_.begin(), monitors_.end(), [&](const MonitorRegion& region) {
                    return region.monitorId == monitor->m_id;
                });
            }
        }
        if (regionIt == monitors_.end())
            continue;

        WorkspaceNode node;
        node.workspaceId = snapshot.workspaceId;
        node.monitorId = regionIt->monitorId;
        for (const auto& snapshotWindow : snapshot.windows) {
            if (!is_tiled_overview_window(snapshotWindow.window))
                continue;

            Target target;
            target.type = TargetType::Window;
            target.workspaceId = snapshot.workspaceId;
            target.monitorId = regionIt->monitorId;
            target.window = snapshotWindow.window;
            target.box = snapshotWindow.box;
            target.synthetic = false;
            node.targets.push_back(std::move(target));
        }

        if (node.targets.empty())
            continue;

        regionIt->workspaces.push_back(std::move(node));
    }

    for (auto& region : monitors_) {
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

    prepareAllCanvasesForOverview();
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
    if (targets.empty())
        return nullptr;

    std::vector<OverviewLogic::TargetCandidate> candidates;
    candidates.reserve(targets.size());
    auto currentIndex = size_t{0};
    auto foundCurrent = false;
    for (size_t index = 0; index < targets.size(); ++index) {
        const auto* target = targets[index];
        candidates.push_back({.monitorId = target->monitorId, .box = target->box});
        if (!foundCurrent && same_target(*target, *selection_)) {
            currentIndex = index;
            foundCurrent = true;
        }
    }

    if (!foundCurrent)
        return nullptr;

    const auto nextIndex = OverviewLogic::pickTargetIndex(candidates, currentIndex, direction);
    if (!nextIndex)
        return nullptr;

    return targets[*nextIndex];
}

bool Session::createSyntheticEmptyTarget(Direction direction) {
    if (!selection_)
        return false;

    std::vector<OverviewLogic::RegionCandidate> regions;
    regions.reserve(monitors_.size());
    auto currentRegionIndex = size_t{0};
    auto foundCurrentRegion = false;
    for (size_t index = 0; index < monitors_.size(); ++index) {
        const auto& region = monitors_[index];
        regions.push_back({.monitorId = region.monitorId, .box = region.box});
        if (!foundCurrentRegion && region.monitorId == selection_->monitorId) {
            currentRegionIndex = index;
            foundCurrentRegion = true;
        }
    }

    if (!foundCurrentRegion)
        return false;

    const auto regionIndex = OverviewLogic::pickRegionIndexForSyntheticTarget(regions, currentRegionIndex, selection_->box, direction);
    if (!regionIndex)
        return false;

    const auto& region = monitors_[*regionIndex];

    Target target;
    target.type = TargetType::EmptyWorkspace;
    target.workspaceId = nextWorkspaceId();
    target.monitorId = region.monitorId;
    target.window = nullptr;
    target.box = OverviewLogic::buildSyntheticTargetBox(regions[*regionIndex], selection_->box, direction);
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
        const auto acceptPlan = OverviewLogic::buildEmptyAcceptPlan(selection_->monitorId, selection_->workspaceId);
        for (const auto& step : acceptPlan) {
            switch (step.type) {
                case OverviewLogic::AcceptActionType::FocusMonitor:
                    if (const auto monitor = g_pCompositor->getMonitorFromID(step.monitorId))
                        CanvasLayoutInternal::invoke_dispatcher("focusmonitor", monitor->m_name, "overview_accept_empty_monitor");
                    break;
                case OverviewLogic::AcceptActionType::Workspace:
                    CanvasLayoutInternal::invoke_dispatcher("workspace", std::to_string(step.workspaceId), "overview_accept_empty_workspace");
                    break;
            }
        }
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
