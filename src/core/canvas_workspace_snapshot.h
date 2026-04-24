#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ScrollerCanvasSnapshot {

constexpr int kFormatVersion = 1;

struct MonitorMemberSnapshot {
    int  monitorId = -1;
    int  workspaceId = -1;
    bool special = false;
};

struct CanvasWorkspaceSnapshot {
    int                               canvasId = -1;
    int                               tileX = 0;
    int                               tileY = 0;
    std::vector<MonitorMemberSnapshot> members;
};

struct RepositorySnapshot {
    int                                version = kFormatVersion;
    int                                activeCanvasId = -1;
    std::vector<CanvasWorkspaceSnapshot> canvases;
};

std::string                      serialize_repository(const RepositorySnapshot& snapshot);
std::optional<RepositorySnapshot> deserialize_repository(std::string_view data);

} // namespace ScrollerCanvasSnapshot
