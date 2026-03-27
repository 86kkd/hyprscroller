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

int resolved_monitor_id(PHLWORKSPACE workspace, PHLWINDOW window, int fallbackMonitorId) {
    if (window) {
        if (const auto monitor = g_pCompositor->getMonitorFromID(window->monitorID()))
            return monitor->m_id;
    }

    if (workspace) {
        if (const auto monitor = CanvasLayoutInternal::visible_monitor_for_workspace(workspace))
            return monitor->m_id;

        if (const auto workspaceMonitor = g_pCompositor->getMonitorFromID(workspace->monitorID()))
            return workspaceMonitor->m_id;
    }

    return fallbackMonitorId;
}

bool execute_accept_plan(const std::vector<OverviewLogic::AcceptAction>& plan, PHLWORKSPACE workspace, const char* context) {
    for (const auto& step : plan) {
        switch (step.type) {
            case OverviewLogic::AcceptActionType::FocusMonitor:
                if (const auto monitor = g_pCompositor->getMonitorFromID(step.monitorId)) {
                    if (!monitor->m_name.empty())
                        (void)CanvasLayoutInternal::invoke_dispatcher("focusmonitor", monitor->m_name, context);
                }
                break;
            case OverviewLogic::AcceptActionType::Workspace: {
                const auto selector = workspace ? workspace_selector(workspace) : std::to_string(step.workspaceId);
                (void)CanvasLayoutInternal::invoke_dispatcher("workspace", selector, context);
                break;
            }
            case OverviewLogic::AcceptActionType::ToggleSpecialWorkspace: {
                const auto selector = workspace ? workspace_selector(workspace) : std::to_string(step.workspaceId);
                (void)CanvasLayoutInternal::invoke_dispatcher("togglespecialworkspace", selector, context);
                break;
            }
        }
    }

    return true;
}

const MonitorRegion* initial_empty_region(const Model& model) {
    if (model.monitors().empty())
        return nullptr;

    const auto cursorMonitor = g_pCompositor->getMonitorFromCursor();
    if (!cursorMonitor)
        return &model.monitors().front();

    const auto* region = model.regionForMonitor(cursorMonitor->m_id);
    return region ? region : &model.monitors().front();
}

Target initial_empty_target(const Model& model, const MonitorRegion& region) {
    return makeEmptyTarget(model.nextWorkspaceId(),
                           region.monitorId,
                           {region.box.x + region.box.w * 0.16,
                            region.box.y + region.box.h * 0.16,
                            region.box.w * 0.68,
                            region.box.h * 0.68},
                           true);
}

bool try_select_origin_window(Model& model) {
    const auto originWindow = model.origin().window;
    if (!originWindow)
        return false;

    const auto ref = model.findByWindow(originWindow);
    if (!ref)
        return false;

    model.setSelection(*ref);
    return true;
}

bool try_select_origin_workspace(Model& model) {
    const auto originWorkspace = model.origin().workspaceId;
    if (originWorkspace == WORKSPACE_INVALID)
        return false;

    const auto ref = model.findByWorkspace(originWorkspace);
    if (!ref)
        return false;

    model.setSelection(*ref);
    return true;
}

void focus_workspace_target(PHLWORKSPACE workspace, WORKSPACEID workspaceId, int monitorId, const char* context) {
    const auto acceptPlan = workspace
        ? OverviewLogic::buildWorkspaceAcceptPlan(monitorId, workspace->m_id, workspace->m_isSpecialWorkspace)
        : OverviewLogic::buildEmptyAcceptPlan(monitorId, workspaceId);
    execute_accept_plan(acceptPlan, workspace, context);
}

} // namespace

bool Session::active() const {
    return active_;
}

const Model& Session::model() const {
    return model_;
}

void Session::damageMonitors() const {
    for (const auto& region : model_.monitors()) {
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
    model_.clear();
}

bool Session::selectInitialTarget() {
    const auto& targetGraph = model_.targetGraph();
    if (targetGraph.empty()) {
        const auto* region = initial_empty_region(model_);
        if (!region)
            return false;

        model_.setSyntheticSelection(initial_empty_target(model_, *region));
        return true;
    }

    if (try_select_origin_window(model_))
        return true;

    if (try_select_origin_workspace(model_))
        return true;

    if (const auto ref = model_.firstTarget()) {
        model_.setSelection(*ref);
        return true;
    }

    return false;
}

void Session::open() {
    if (active_)
        return;

    auto originWorkspace = CanvasLayoutInternal::get_workspace_id();
    auto originWindow = PHLWINDOW{};
    auto originMonitor = MONITOR_INVALID;

    if (const auto workspace = g_pCompositor->getWorkspaceByID(originWorkspace)) {
        originWindow = workspace->getLastFocusedWindow();
        originMonitor = resolved_monitor_id(workspace, originWindow, workspace->monitorID());
    }

    if (originMonitor == MONITOR_INVALID) {
        if (const auto monitor = g_pCompositor->getMonitorFromCursor())
            originMonitor = monitor->m_id;
    }

    model_.setOrigin(originMonitor, originWorkspace, originWindow);
    prepareAllCanvasesForOverview();
    model_.rebuild();
    if (!selectInitialTarget()) {
        clear();
        spdlog::warn("overview_open: no targets available");
        return;
    }

    active_ = true;
    damageMonitors();
    const auto* selection = model_.selection();
    spdlog::info("overview_open: origin_workspace={} origin_window={} monitors={} selection_workspace={} selection_window={} synthetic={}",
                 model_.origin().workspaceId,
                 static_cast<const void*>(model_.origin().window ? model_.origin().window.get() : nullptr),
                 model_.monitors().size(),
                 selection ? selection->workspaceId : WORKSPACE_INVALID,
                 static_cast<const void*>(selection && selection->window ? selection->window.get() : nullptr),
                 selection ? selection->synthetic : false);
}

std::optional<TargetRef> Session::findBestTarget(Direction direction) const {
    if (!model_.selectionRef())
        return std::nullopt;

    const auto& targetGraph = model_.targetGraph();
    if (targetGraph.empty())
        return std::nullopt;

    std::vector<OverviewLogic::TargetCandidate> candidates;
    candidates.reserve(targetGraph.size());
    auto currentIndex = size_t{0};
    auto foundCurrent = false;
    for (size_t index = 0; index < targetGraph.size(); ++index) {
        const auto& target = targetGraph[index];
        candidates.push_back({.monitorId = target.monitorId, .box = target.box});
        if (!foundCurrent && target.ref == *model_.selectionRef()) {
            currentIndex = index;
            foundCurrent = true;
        }
    }

    if (!foundCurrent)
        return std::nullopt;

    const auto nextIndex = OverviewLogic::pickTargetIndex(candidates, currentIndex, direction);
    if (!nextIndex)
        return std::nullopt;

    return targetGraph[*nextIndex].ref;
}

bool Session::createSyntheticEmptyTarget(Direction direction) {
    const auto* selection = model_.selection();
    if (!selection)
        return false;

    std::vector<OverviewLogic::RegionCandidate> regions;
    regions.reserve(model_.monitors().size());
    auto currentRegionIndex = size_t{0};
    auto foundCurrentRegion = false;
    for (size_t index = 0; index < model_.monitors().size(); ++index) {
        const auto& region = model_.monitors()[index];
        regions.push_back({.monitorId = region.monitorId, .box = region.box});
        if (!foundCurrentRegion && region.monitorId == selection->monitorId) {
            currentRegionIndex = index;
            foundCurrentRegion = true;
        }
    }

    if (!foundCurrentRegion)
        return false;

    const auto regionIndex = OverviewLogic::pickRegionIndexForSyntheticTarget(regions, currentRegionIndex, selection->box, direction);
    if (!regionIndex)
        return false;

    const auto& region = model_.monitors()[*regionIndex];
    model_.setSyntheticSelection(makeEmptyTarget(model_.nextWorkspaceId(),
                                                 region.monitorId,
                                                 OverviewLogic::buildSyntheticTargetBox(regions[*regionIndex], selection->box, direction),
                                                 true));
    const auto* syntheticSelection = model_.selection();
    spdlog::info("overview_create_empty: workspace={} monitor={} box=({}, {}, {}, {})",
                 syntheticSelection ? syntheticSelection->workspaceId : WORKSPACE_INVALID,
                 syntheticSelection ? syntheticSelection->monitorId : MONITOR_INVALID,
                 syntheticSelection ? syntheticSelection->box.x : 0.0,
                 syntheticSelection ? syntheticSelection->box.y : 0.0,
                 syntheticSelection ? syntheticSelection->box.w : 0.0,
                 syntheticSelection ? syntheticSelection->box.h : 0.0);
    return true;
}

bool Session::moveSelection(Direction direction) {
    if (!active_ || !model_.selection())
        return false;

    if (const auto targetRef = findBestTarget(direction)) {
        model_.setSelection(*targetRef);
        if (const auto* selection = model_.selection(); selection && !selection->synthetic)
            model_.clearSyntheticSelection();
        const auto* selection = model_.selection();
        spdlog::info("overview_move: direction={} workspace={} window={} synthetic={}",
                     ScrollerCore::direction_name(direction),
                     selection ? selection->workspaceId : WORKSPACE_INVALID,
                     static_cast<const void*>(selection && selection->window ? selection->window.get() : nullptr),
                     selection ? selection->synthetic : false);
        damageMonitors();
        return true;
    }

    const auto created = createSyntheticEmptyTarget(direction);
    if (created)
        damageMonitors();
    return created;
}

void Session::acceptSelection() {
    const auto* selection = model_.selection();
    if (!selection)
        return;

    const auto workspace = g_pCompositor->getWorkspaceByID(selection->workspaceId);
    const auto monitorId = resolved_monitor_id(workspace, selection->window, selection->monitorId);

    if (selection->type == TargetType::EmptyWorkspace) {
        focus_workspace_target(workspace, selection->workspaceId, monitorId, "overview_accept_empty");
        spdlog::info("overview_accept_empty: workspace={} monitor={} synthetic={}",
                     selection->workspaceId,
                     monitorId,
                     selection->synthetic);
        return;
    }

    if (!workspace || !selection->window) {
        spdlog::warn("overview_accept_window: invalid target workspace={} window={}",
                     selection->workspaceId,
                     static_cast<const void*>(selection->window ? selection->window.get() : nullptr));
        return;
    }

    focus_workspace_target(workspace, workspace->m_id, monitorId, "overview_accept_window");
    CanvasLayoutInternal::switch_to_window(selection->window, true);
    spdlog::info("overview_accept_window: workspace={} window={} special={}",
                 selection->workspaceId,
                 static_cast<const void*>(selection->window.get()),
                 workspace->m_isSpecialWorkspace);
}

void Session::restoreOrigin() {
    const auto workspace = g_pCompositor->getWorkspaceByID(model_.origin().workspaceId);
    const auto monitorId = resolved_monitor_id(workspace, model_.origin().window, model_.origin().monitorId);

    if (model_.origin().window && model_.origin().window->m_isMapped && workspace) {
        focus_workspace_target(workspace, workspace->m_id, monitorId, "overview_restore_origin_window");
        CanvasLayoutInternal::switch_to_window(model_.origin().window, false);
        spdlog::info("overview_restore_origin_window: workspace={} monitor={} window={}",
                     workspace->m_id,
                     monitorId,
                     static_cast<const void*>(model_.origin().window.get()));
        return;
    }

    if (!workspace)
        return;

    focus_workspace_target(workspace, workspace->m_id, monitorId, "overview_restore_origin_workspace");
    spdlog::info("overview_restore_origin_workspace: workspace={} monitor={} special={}",
                 workspace->m_id,
                 monitorId,
                 workspace->m_isSpecialWorkspace);
}

void Session::close(bool acceptSelectionFlag) {
    if (!active_)
        return;

    if (acceptSelectionFlag)
        acceptSelection();
    else
        restoreOrigin();

    const auto* selection = model_.selection();
    spdlog::info("overview_close: accepted={} selection_workspace={} selection_window={}",
                 acceptSelectionFlag,
                 selection ? selection->workspaceId : WORKSPACE_INVALID,
                 static_cast<const void*>(selection && selection->window ? selection->window.get() : nullptr));
    damageMonitors();
    clear();
}

} // namespace Overview
