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
 * - `effects.cpp` performs real Hyprland side effects on accept/restore
 */
#include "overview/session/session.h"

#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <spdlog/spdlog.h>

#include "overview/navigation/logic.h"
#include "overview/session/effects.h"
#include "overview/navigation/selection.h"

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
    if (originWorkspace == INVALID_WORKSPACE_ID)
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
    inputHandling_.reset();
    model_.clear();
}

void Session::markInputHandled() {
    inputHandling_.markHandled();
}

bool Session::consumeInputHandled(bool released) {
    return inputHandling_.consume(released);
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
                 selection ? selection->workspaceId : INVALID_WORKSPACE_ID,
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
                     selection ? selection->workspaceId : INVALID_WORKSPACE_ID,
                     static_cast<const void*>(selection && selection->window ? selection->window.get() : nullptr),
                     selection ? selection->synthetic : false);
        damageMonitors();
        return true;
    }

    // Overview navigation is preview-only: reaching an edge should stop rather
    // than synthesizing a brand-new workspace candidate.
    return false;
}

bool Session::acceptSelection() {
    const auto* selection = model_.selection();
    if (!selection)
        return false;

    // Real side effects live in `effects.cpp`; keeping that split makes
    // this file about control flow rather than compositor mutation details.
    return SessionEffects::acceptTarget(*selection);
}

bool Session::restoreOrigin() {
    return SessionEffects::restoreOrigin(model_.origin());
}

void Session::close(bool acceptSelectionFlag) {
    if (!active_)
        return;

    // Overview always resolves exactly once on close: either accept the current
    // selection or restore the remembered origin. Only after that do we clear
    // the logical model and remove the overlay.
    auto resolved = acceptSelectionFlag
        ? acceptSelection()
        : restoreOrigin();
    if (acceptSelectionFlag && !resolved) {
        spdlog::warn("overview_close: accept failed, restoring origin");
        resolved = restoreOrigin();
    }

    const auto* selection = model_.selection();
    spdlog::info("overview_close: accepted={} resolved={} selection_workspace={} selection_window={}",
                 acceptSelectionFlag,
                 resolved,
                 selection ? selection->workspaceId : INVALID_WORKSPACE_ID,
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
                 selection ? selection->workspaceId : INVALID_WORKSPACE_ID,
                 static_cast<const void*>(selection && selection->window ? selection->window.get() : nullptr));
    damageMonitors();
    clear();
}

} // namespace Overview
