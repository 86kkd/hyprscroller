/**
 * @file overview/navigation/selection.cpp
 * @brief Pure session-selection helpers used by overview session control flow.
 */
#include "overview/navigation/selection.h"

namespace Overview {

void InputHandlingState::markHandled() {
    handled_ = true;
    pendingRelease_ = true;
}

bool InputHandlingState::consume(bool released) {
    const auto handled = handled_ || (released && pendingRelease_);
    handled_ = false;

    if (released)
        pendingRelease_ = false;

    return handled;
}

void InputHandlingState::reset() {
    handled_ = false;
    pendingRelease_ = false;
}

InitialSelectionChoice chooseInitialSelectionChoice(bool hasTargets,
                                                    bool hasOriginWindowTarget,
                                                    bool hasOriginWorkspaceTarget,
                                                    bool hasFirstTarget,
                                                    bool hasInitialEmptyRegion) {
    // Selection priority mirrors user expectations:
    // 1. return to the exact origin window when possible
    // 2. else return to the origin workspace
    // 3. else pick the first concrete target
    // 4. else fall back to an initial empty region placeholder
    if (!hasTargets)
        return hasInitialEmptyRegion ? InitialSelectionChoice::InitialEmpty : InitialSelectionChoice::None;

    if (hasOriginWindowTarget)
        return InitialSelectionChoice::OriginWindow;

    if (hasOriginWorkspaceTarget)
        return InitialSelectionChoice::OriginWorkspace;

    if (hasFirstTarget)
        return InitialSelectionChoice::FirstTarget;

    return InitialSelectionChoice::None;
}

bool shouldCloseOverviewOnKeyRelease(bool overviewActive,
                                     bool released,
                                     bool updateModsOnly,
                                     bool handledByOverview,
                                     bool noModifiersRemaining) {
    if (!overviewActive || !released || handledByOverview)
        return false;

    if (!updateModsOnly)
        return true;

    return noModifiersRemaining;
}

} // namespace Overview
