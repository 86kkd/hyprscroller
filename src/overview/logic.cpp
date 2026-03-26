#include "logic.h"

#include <cmath>
#include <limits>

namespace OverviewLogic {
namespace {

double center_x(const ScrollerCore::Box& box) {
    return box.x + box.w / 2.0;
}

double center_y(const ScrollerCore::Box& box) {
    return box.y + box.h / 2.0;
}

bool is_in_direction(const ScrollerCore::Box& from, const ScrollerCore::Box& candidate, Direction direction) {
    switch (direction) {
        case Direction::Left:
            return center_x(candidate) < center_x(from);
        case Direction::Right:
            return center_x(candidate) > center_x(from);
        case Direction::Up:
            return center_y(candidate) < center_y(from);
        case Direction::Down:
            return center_y(candidate) > center_y(from);
        default:
            return false;
    }
}

double primary_distance(const ScrollerCore::Box& from, const ScrollerCore::Box& candidate, Direction direction) {
    switch (direction) {
        case Direction::Left:
            return center_x(from) - center_x(candidate);
        case Direction::Right:
            return center_x(candidate) - center_x(from);
        case Direction::Up:
            return center_y(from) - center_y(candidate);
        case Direction::Down:
            return center_y(candidate) - center_y(from);
        default:
            return std::numeric_limits<double>::infinity();
    }
}

double secondary_distance(const ScrollerCore::Box& from, const ScrollerCore::Box& candidate, Direction direction) {
    switch (direction) {
        case Direction::Left:
        case Direction::Right:
            return std::abs(center_y(candidate) - center_y(from));
        case Direction::Up:
        case Direction::Down:
            return std::abs(center_x(candidate) - center_x(from));
        default:
            return std::numeric_limits<double>::infinity();
    }
}

} // namespace

std::optional<size_t> pickTargetIndex(const std::vector<TargetCandidate>& targets, size_t currentIndex, Direction direction) {
    if (currentIndex >= targets.size())
        return std::nullopt;

    const auto& current = targets[currentIndex];
    auto bestIndex = std::optional<size_t>{};
    auto bestPrimary = std::numeric_limits<double>::infinity();
    auto bestMonitorPenalty = std::numeric_limits<int>::max();
    auto bestSecondary = std::numeric_limits<double>::infinity();

    for (size_t index = 0; index < targets.size(); ++index) {
        if (index == currentIndex)
            continue;

        const auto& candidate = targets[index];
        if (!is_in_direction(current.box, candidate.box, direction))
            continue;

        const auto primary = primary_distance(current.box, candidate.box, direction);
        const auto monitorPenalty = candidate.monitorId == current.monitorId ? 0 : 1;
        const auto secondary = secondary_distance(current.box, candidate.box, direction);

        if (!bestIndex || primary < bestPrimary ||
            (primary == bestPrimary && monitorPenalty < bestMonitorPenalty) ||
            (primary == bestPrimary && monitorPenalty == bestMonitorPenalty && secondary < bestSecondary)) {
            bestIndex = index;
            bestPrimary = primary;
            bestMonitorPenalty = monitorPenalty;
            bestSecondary = secondary;
        }
    }

    return bestIndex;
}

std::optional<size_t> pickRegionIndexForSyntheticTarget(const std::vector<RegionCandidate>& regions, size_t currentRegionIndex,
                                                        const ScrollerCore::Box& sourceBox, Direction direction) {
    if (regions.empty() || currentRegionIndex >= regions.size())
        return std::nullopt;

    const auto& current = regions[currentRegionIndex];
    const auto overflowsCurrentRegion = [&] {
        switch (direction) {
            case Direction::Left:
                return sourceBox.x < current.box.x;
            case Direction::Right:
                return sourceBox.x + sourceBox.w > current.box.x + current.box.w;
            case Direction::Up:
                return sourceBox.y < current.box.y;
            case Direction::Down:
                return sourceBox.y + sourceBox.h > current.box.y + current.box.h;
            default:
                return false;
        }
    };

    if (!overflowsCurrentRegion())
        return currentRegionIndex;

    auto bestIndex = std::optional<size_t>{};
    auto bestPrimary = std::numeric_limits<double>::infinity();
    auto bestSecondary = std::numeric_limits<double>::infinity();

    for (size_t index = 0; index < regions.size(); ++index) {
        if (index == currentRegionIndex)
            continue;

        const auto& candidate = regions[index];
        if (!is_in_direction(current.box, candidate.box, direction))
            continue;

        const auto primary = primary_distance(current.box, candidate.box, direction);
        const auto secondary = secondary_distance(sourceBox, candidate.box, direction);
        if (!bestIndex || primary < bestPrimary || (primary == bestPrimary && secondary < bestSecondary)) {
            bestIndex = index;
            bestPrimary = primary;
            bestSecondary = secondary;
        }
    }

    if (bestIndex)
        return bestIndex;

    return currentRegionIndex;
}

ScrollerCore::Box buildSyntheticTargetBox(const RegionCandidate& region, const ScrollerCore::Box& sourceBox, Direction direction) {
    auto box = sourceBox;
    const auto stepX = std::max(box.w, region.box.w * 0.35);
    const auto stepY = std::max(box.h, region.box.h * 0.35);

    switch (direction) {
        case Direction::Left:
            box.x -= stepX;
            break;
        case Direction::Right:
            box.x += stepX;
            break;
        case Direction::Up:
            box.y -= stepY;
            break;
        case Direction::Down:
            box.y += stepY;
            break;
        default:
            break;
    }

    box.w = std::min(box.w, region.box.w);
    box.h = std::min(box.h, region.box.h);
    box.x = std::clamp(box.x, region.box.x, region.box.x + std::max(0.0, region.box.w - box.w));
    box.y = std::clamp(box.y, region.box.y, region.box.y + std::max(0.0, region.box.h - box.h));
    return box;
}

std::vector<AcceptAction> buildEmptyAcceptPlan(int monitorId, WorkspaceId workspaceId) {
    return {
        {.type = AcceptActionType::FocusMonitor, .monitorId = monitorId, .workspaceId = WORKSPACE_ID_INVALID},
        {.type = AcceptActionType::Workspace, .monitorId = MONITOR_ID_INVALID, .workspaceId = workspaceId},
    };
}

std::vector<AcceptAction> buildWorkspaceAcceptPlan(int monitorId, WorkspaceId workspaceId, bool specialWorkspace) {
    auto plan = std::vector<AcceptAction>{
        {.type = AcceptActionType::FocusMonitor, .monitorId = monitorId, .workspaceId = WORKSPACE_ID_INVALID},
    };

    plan.push_back({
        .type = specialWorkspace ? AcceptActionType::ToggleSpecialWorkspace : AcceptActionType::Workspace,
        .monitorId = MONITOR_ID_INVALID,
        .workspaceId = workspaceId,
    });
    return plan;
}

} // namespace OverviewLogic
