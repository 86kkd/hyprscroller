/**
 * @file session_selection.cpp
 * @brief Pure session-selection helpers used by overview session control flow.
 */
#include "session_selection.h"

namespace Overview {

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

bool shouldDismissOnKeyRelease(bool overviewActive,
                               bool released,
                               bool updateModsOnly,
                               bool handledByOverview) {
    return overviewActive && released && !updateModsOnly && !handledByOverview;
}

} // namespace Overview
