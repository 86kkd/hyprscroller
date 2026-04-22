/**
 * @file logic.cpp
 * @brief Pure directional-target selection and accept-plan helpers for overview.
 *
 * This file is intentionally renderer-free and compositor-free. It answers
 * questions such as:
 * - which target should directional navigation land on?
 * - which monitor region should host a synthetic empty-workspace target?
 * - which ordered dispatcher steps should be executed when the user accepts?
 */
#include "logic.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace OverviewLogic {
namespace {

// Overview navigation compares item centers rather than edges so targets of
// different sizes can still be ranked consistently.
double center_x(const ScrollerCore::Box& box) {
    return box.x + box.w / 2.0;
}

double center_y(const ScrollerCore::Box& box) {
    return box.y + box.h / 2.0;
}

double overlap_length(double a0, double a1, double b0, double b1) {
    return std::max(0.0, std::min(a1, b1) - std::max(a0, b0));
}

double beam_overlap(const ScrollerCore::Box& from, const ScrollerCore::Box& candidate, Direction direction) {
    switch (direction) {
        case Direction::Left:
        case Direction::Right:
            return overlap_length(from.y, from.y + from.h, candidate.y, candidate.y + candidate.h);
        case Direction::Up:
        case Direction::Down:
            return overlap_length(from.x, from.x + from.w, candidate.x, candidate.x + candidate.w);
        default:
            return 0.0;
    }
}

bool nearly_equal(double lhs, double rhs, double epsilon = 1e-6) {
    return std::abs(lhs - rhs) <= epsilon;
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

// Primary distance measures "how far along the navigation axis" the candidate
// sits from the current target. Lower is always better.
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

// Secondary distance breaks ties using the perpendicular axis so horizontally
// aligned moves prefer targets that are also vertically aligned, and vice versa.
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
    auto bestBeamOverlap = 0.0;
    auto bestPrimary = std::numeric_limits<double>::infinity();
    auto bestMonitorPenalty = std::numeric_limits<int>::max();
    auto bestSecondary = std::numeric_limits<double>::infinity();

    // Ranking order:
    // 1. candidate must lie in the requested direction
    // 2. candidates that overlap the directional beam (same row/column) win
    // 3. nearest candidate on the primary axis wins
    // 4. same-monitor targets beat cross-monitor ones
    // 5. better perpendicular alignment breaks final ties
    for (size_t index = 0; index < targets.size(); ++index) {
        if (index == currentIndex)
            continue;

        const auto& candidate = targets[index];
        if (!is_in_direction(current.box, candidate.box, direction))
            continue;

        const auto beam = beam_overlap(current.box, candidate.box, direction);
        const auto primary = primary_distance(current.box, candidate.box, direction);
        const auto monitorPenalty = candidate.monitorId == current.monitorId ? 0 : 1;
        const auto secondary = secondary_distance(current.box, candidate.box, direction);
        const auto beamAligned = beam > 0.0;
        const auto bestBeamAligned = bestBeamOverlap > 0.0;

        if (!bestIndex ||
            (beamAligned && !bestBeamAligned) ||
            (beamAligned == bestBeamAligned && primary < bestPrimary && !nearly_equal(primary, bestPrimary)) ||
            (beamAligned == bestBeamAligned && nearly_equal(primary, bestPrimary) && monitorPenalty < bestMonitorPenalty) ||
            (beamAligned == bestBeamAligned && nearly_equal(primary, bestPrimary) && monitorPenalty == bestMonitorPenalty &&
             beamAligned && beam > bestBeamOverlap && !nearly_equal(beam, bestBeamOverlap)) ||
            (beamAligned == bestBeamAligned && nearly_equal(primary, bestPrimary) && monitorPenalty == bestMonitorPenalty &&
             (!beamAligned || nearly_equal(beam, bestBeamOverlap)) && secondary < bestSecondary)) {
            bestIndex = index;
            bestBeamOverlap = beam;
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
    // Synthetic targets only leave the current region when the proposed box
    // would overflow that region in the requested direction.
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

    // Region selection is simpler than concrete target selection: regions are
    // already monitor-sized buckets, so we only compare directional proximity
    // and then alignment with the source box.
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
    // Advance by at least one source-box size, but also by a fraction of the
    // destination region so very small source boxes still move perceptibly.
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

    // Synthetic empty-workspace targets must stay fully inside the destination
    // region because later hit-testing assumes valid in-bounds boxes.
    box.w = std::min(box.w, region.box.w);
    box.h = std::min(box.h, region.box.h);
    box.x = std::clamp(box.x, region.box.x, region.box.x + std::max(0.0, region.box.w - box.w));
    box.y = std::clamp(box.y, region.box.y, region.box.y + std::max(0.0, region.box.h - box.h));
    return box;
}

std::vector<AcceptAction> buildEmptyAcceptPlan(int monitorId, WorkspaceId workspaceId) {
    // Empty targets create/focus a workspace but do not need any follow-up
    // window activation step.
    return {
        {.type = AcceptActionType::FocusMonitor, .monitorId = monitorId, .workspaceId = WORKSPACE_ID_INVALID},
        {.type = AcceptActionType::Workspace, .monitorId = MONITOR_ID_INVALID, .workspaceId = workspaceId},
    };
}

std::vector<AcceptAction> buildWorkspaceAcceptPlan(int monitorId, WorkspaceId workspaceId, bool specialWorkspace) {
    // Existing workspaces always start with monitor focus, then either switch to
    // a regular workspace or toggle a special workspace on that monitor.
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
