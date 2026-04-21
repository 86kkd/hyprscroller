/**
 * @file session.cpp
 * @brief Global overview-session construction and logical target navigation.
 *
 * This file builds a monitor-scoped preview model from the current set of
 * tiled windows, keeps a logical selection independent from Hyprland focus,
 * and resolves the final workspace/window jump only when overview closes with
 * acceptance.
 *
 * Reading guide:
 * - `session.cpp` is the control-flow layer for overview
 * - `model.cpp` builds the read-only target graph
 * - `scene.cpp` reshapes that graph into render DTOs
 * - `session_effects.cpp` performs real Hyprland side effects on accept/restore
 */
#include "session.h"

#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <spdlog/spdlog.h>

#include "logic.h"
#include "session_effects.h"
#include "session_selection.h"

namespace Overview {
namespace {

// When overview opens with no existing targets selected, prefer the monitor
// under the cursor so a synthetic empty target appears where the user is
// already interacting.
const MonitorRegion* initial_empty_region(const Model& model) {
    if (model.monitors().empty())
        return nullptr;

    const auto cursorMonitor = g_pCompositor->getMonitorFromCursor();
    if (!cursorMonitor)
        return &model.monitors().front();

    const auto* region = model.regionForMonitor(cursorMonitor->m_id);
    return region ? region : &model.monitors().front();
}

// Synthetic empty targets are intentionally smaller than the full monitor box so
// they render as a clear "new workspace candidate" instead of a full-screen fill.
Target initial_empty_target(const MonitorRegion& region) {
    return makeEmptyTarget(SessionEffects::nextWorkspaceId(),
                           region.monitorId,
                           {region.box.x + region.box.w * 0.16,
                            region.box.y + region.box.h * 0.16,
                            region.box.w * 0.68,
                            region.box.h * 0.68},
                           true);
}

// Initial selection prefers the concrete origin window when it is still present
// in the rebuilt model.
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

// If the origin window disappeared, fall back to "same workspace" before using
// an arbitrary first target.
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
    inputHandled_ = false;
    model_.clear();
}

void Session::markInputHandled() {
    inputHandled_ = true;
}

bool Session::consumeInputHandled() {
    const auto handled = inputHandled_;
    inputHandled_ = false;
    return handled;
}

bool Session::selectInitialTarget() {
    // `chooseInitialSelectionChoice` stays pure and testable. This method turns
    // that decision into actual model mutation in one place.
    const auto choice = chooseInitialSelectionChoice(!model_.targetGraph().empty(),
                                                     model_.findByWindow(model_.origin().window).has_value(),
                                                     model_.findByWorkspace(model_.origin().workspaceId).has_value(),
                                                     model_.firstTarget().has_value(),
                                                     initial_empty_region(model_) != nullptr);

    switch (choice) {
        case InitialSelectionChoice::OriginWindow:
            return try_select_origin_window(model_);
        case InitialSelectionChoice::OriginWorkspace:
            return try_select_origin_workspace(model_);
        case InitialSelectionChoice::FirstTarget:
            if (const auto ref = model_.firstTarget()) {
                model_.setSelection(*ref);
                return true;
            }
            return false;
        case InitialSelectionChoice::InitialEmpty:
            if (const auto* region = initial_empty_region(model_)) {
                model_.setSyntheticSelection(initial_empty_target(*region));
                return true;
            }
            return false;
        case InitialSelectionChoice::None:
        default:
            return false;
    }
}

void Session::open() {
    if (active_)
        return;

    // Open happens in three phases:
    // 1. capture enough origin state to restore or accept later
    // 2. prepare/rebuild the read-only logical overview model
    // 3. choose an initial selection and start damaging monitors for rendering
    const auto origin = SessionEffects::captureOrigin();
    model_.setOrigin(origin.monitorId, origin.workspaceId, origin.window);
    SessionEffects::prepareSnapshots();
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

    // Convert the richer overview target graph into the minimal pure-routing
    // representation that `OverviewLogic` understands.
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

    // Synthetic targets are routed at monitor-region granularity rather than
    // real-target granularity. The current selection box becomes the seed for
    // where the "empty workspace" box should appear next.
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
    model_.setSyntheticSelection(makeEmptyTarget(SessionEffects::nextWorkspaceId(),
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

    // Real side effects live in `session_effects.cpp`; keeping that split makes
    // this file about control flow rather than compositor mutation details.
    SessionEffects::acceptTarget(*selection);
}

void Session::restoreOrigin() {
    SessionEffects::restoreOrigin(model_.origin());
}

void Session::close(bool acceptSelectionFlag) {
    if (!active_)
        return;

    // Overview always resolves exactly once on close: either accept the current
    // selection or restore the remembered origin. Only after that do we clear
    // the logical model and remove the overlay.
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

void Session::dismiss() {
    if (!active_)
        return;

    // Dismiss differs from close(false): it simply tears the overlay down
    // without running accept/restore side effects.
    const auto* selection = model_.selection();
    spdlog::info("overview_dismiss: selection_workspace={} selection_window={}",
                 selection ? selection->workspaceId : WORKSPACE_INVALID,
                 static_cast<const void*>(selection && selection->window ? selection->window.get() : nullptr));
    damageMonitors();
    clear();
}

} // namespace Overview
