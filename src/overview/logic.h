/**
 * @file logic.h
 * @brief Pure overview navigation helpers shared by session code and tests.
 */
#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#include "../core/direction.h"
#include "../core/types.h"

namespace OverviewLogic {

using WorkspaceId = int64_t;

constexpr WorkspaceId WORKSPACE_ID_INVALID = -1;
constexpr int         MONITOR_ID_INVALID = -1;

struct TargetCandidate {
    int               monitorId = MONITOR_ID_INVALID;
    ScrollerCore::Box box;
};

struct RegionCandidate {
    int               monitorId = MONITOR_ID_INVALID;
    ScrollerCore::Box box;
};

enum class AcceptActionType {
    FocusMonitor,
    Workspace,
    ToggleSpecialWorkspace,
};

struct AcceptAction {
    AcceptActionType type = AcceptActionType::Workspace;
    int              monitorId = MONITOR_ID_INVALID;
    WorkspaceId      workspaceId = WORKSPACE_ID_INVALID;
};

std::optional<size_t> pickTargetIndex(const std::vector<TargetCandidate>& targets, size_t currentIndex, Direction direction);
std::optional<size_t> pickRegionIndexForSyntheticTarget(const std::vector<RegionCandidate>& regions, size_t currentRegionIndex,
                                                        const ScrollerCore::Box& sourceBox, Direction direction);
ScrollerCore::Box     buildSyntheticTargetBox(const RegionCandidate& region, const ScrollerCore::Box& sourceBox,
                                              Direction direction);
std::vector<AcceptAction> buildWorkspaceAcceptPlan(int monitorId, WorkspaceId workspaceId, bool specialWorkspace);
std::vector<AcceptAction> buildEmptyAcceptPlan(int monitorId, WorkspaceId workspaceId);

} // namespace OverviewLogic
