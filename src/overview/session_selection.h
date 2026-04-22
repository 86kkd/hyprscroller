/**
 * @file session_selection.h
 * @brief Pure session-selection helpers used by overview session control flow.
 */
#pragma once

namespace Overview {

enum class InitialSelectionChoice {
    None,
    OriginWindow,
    OriginWorkspace,
    FirstTarget,
    InitialEmpty,
};

InitialSelectionChoice chooseInitialSelectionChoice(bool hasTargets,
                                                    bool hasOriginWindowTarget,
                                                    bool hasOriginWorkspaceTarget,
                                                    bool hasFirstTarget,
                                                    bool hasInitialEmptyRegion);

bool shouldDismissOnKeyRelease(bool overviewActive,
                               bool released,
                               bool updateModsOnly,
                               bool handledByOverview);

} // namespace Overview
