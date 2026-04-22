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
bool        acceptTarget(const Target& selection);
bool        restoreOrigin(const OriginState& origin);

} // namespace Overview::SessionEffects
