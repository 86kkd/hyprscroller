#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "types.h"

namespace ScrollerSnapshot {

constexpr int kLegacyFormatVersion = 1;
constexpr int kFormatVersion = 2;

struct WindowSnapshot {
    uintptr_t key = 0;
    int       heightMode = 0;
    double    geomY = 0.0;
    double    geomH = 0.0;
    double    memY = 0.0;
    double    memH = 0.0;
};

struct StackSnapshot {
    int                          width = 0;
    int                          reorder = 0;
    bool                         fullscreened = false;
    bool                         maximized = false;
    ScrollerCore::Box            geom;
    ScrollerCore::Box            memGeom;
    uintptr_t                    activeWindowKey = 0;
    std::vector<WindowSnapshot>  windows;
};

struct LaneSnapshot {
    int                         mode = 0;
    int                         reorder = 0;
    bool                        ephemeral = false;
    size_t                      activeStackIndex = 0;
    std::vector<StackSnapshot>  stacks;
};

struct GridItemSnapshot {
    uintptr_t key = 0;
    int       column = 0;
    int       row = 0;
    int       columnSpan = 1;
    int       rowSpan = 1;
};

struct GridSnapshot {
    bool                          enabled = false;
    int                           activeItemIndex = -1;
    int                           viewportColumn = 0;
    int                           viewportRow = 0;
    std::vector<GridItemSnapshot> items;
};

struct CanvasSnapshot {
    int                        version = kFormatVersion;
    int                        workspaceId = -1;
    size_t                     activeLaneIndex = 0;
    std::vector<LaneSnapshot>  lanes;
    GridSnapshot               grid;
};

using RepositorySnapshot = std::unordered_map<int, CanvasSnapshot>;

std::string serialize_repository(const RepositorySnapshot &snapshots);
std::optional<RepositorySnapshot> deserialize_repository(std::string_view data);

} // namespace ScrollerSnapshot
