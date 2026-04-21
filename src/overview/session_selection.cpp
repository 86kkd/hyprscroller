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

} // namespace Overview
