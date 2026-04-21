/**
 * @file session_effects.h
 * @brief Runtime-facing helpers that apply overview session decisions to Hyprland state.
 */
#pragma once

#include "model.h"

namespace Overview::SessionEffects {

OriginState captureOrigin();
void        prepareSnapshots();
WORKSPACEID nextWorkspaceId();
void        acceptTarget(const Target& selection);
void        restoreOrigin(const OriginState& origin);

} // namespace Overview::SessionEffects
