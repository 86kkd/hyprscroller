#include "canvas_workspace_repository.h"

#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <spdlog/spdlog.h>
#include <string>
#include <system_error>

#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/desktop/Workspace.hpp>
#include <hyprland/src/output/Monitor.hpp>

#include "core/hyprland_runtime.h"
#include "internal.h"

namespace CanvasLayoutState {
namespace {

using SnapshotCanvas = ScrollerCanvasSnapshot::CanvasWorkspaceSnapshot;
using SnapshotMember = ScrollerCanvasSnapshot::MonitorMemberSnapshot;

std::string runtime_base_dir() {
    if (const auto* xdgRuntime = std::getenv("XDG_RUNTIME_DIR"); xdgRuntime && xdgRuntime[0] != '\0')
        return xdgRuntime;
    return "/tmp";
}

std::string hyprland_instance_signature() {
    if (const auto* signature = std::getenv("HYPRLAND_INSTANCE_SIGNATURE"); signature && signature[0] != '\0')
        return signature;
    return "default";
}

CanvasWorkspaceRecord from_snapshot(const SnapshotCanvas& canvas) {
    CanvasWorkspaceRecord record;
    record.canvasId = canvas.canvasId;
    record.tileX = canvas.tileX;
    record.tileY = canvas.tileY;
    record.members.reserve(canvas.members.size());
    for (const auto& member : canvas.members) {
        record.members.push_back({
            .monitorId = member.monitorId,
            .workspaceId = static_cast<WORKSPACEID>(member.workspaceId),
            .special = member.special,
        });
    }
    std::sort(record.members.begin(), record.members.end(), [](const auto& lhs, const auto& rhs) {
        return lhs.monitorId < rhs.monitorId;
    });
    return record;
}

SnapshotCanvas to_snapshot(const CanvasWorkspaceRecord& canvas) {
    SnapshotCanvas snapshot;
    snapshot.canvasId = canvas.canvasId;
    snapshot.tileX = canvas.tileX;
    snapshot.tileY = canvas.tileY;
    snapshot.members.reserve(canvas.members.size());
    for (const auto& member : canvas.members)
        snapshot.members.push_back({
            .monitorId = member.monitorId,
            .workspaceId = static_cast<int>(member.workspaceId),
            .special = member.special,
        });
    std::sort(snapshot.members.begin(), snapshot.members.end(), [](const auto& lhs, const auto& rhs) {
        return lhs.monitorId < rhs.monitorId;
    });
    return snapshot;
}

CanvasWorkspaceRecord* find_canvas(std::vector<CanvasWorkspaceRecord>& canvases, int canvasId) {
    const auto it = std::find_if(canvases.begin(), canvases.end(), [&](const auto& canvas) {
        return canvas.canvasId == canvasId;
    });
    return it == canvases.end() ? nullptr : &*it;
}

bool members_match(std::vector<CanvasWorkspaceMember> lhs, std::vector<CanvasWorkspaceMember> rhs) {
    const auto sortMembers = [](auto& members) {
        std::sort(members.begin(), members.end(), [](const auto& a, const auto& b) {
            if (a.monitorId != b.monitorId)
                return a.monitorId < b.monitorId;
            if (a.workspaceId != b.workspaceId)
                return a.workspaceId < b.workspaceId;
            return a.special < b.special;
        });
    };
    sortMembers(lhs);
    sortMembers(rhs);
    if (lhs.size() != rhs.size())
        return false;
    for (size_t index = 0; index < lhs.size(); ++index) {
        if (lhs[index].monitorId != rhs[index].monitorId ||
            lhs[index].workspaceId != rhs[index].workspaceId ||
            lhs[index].special != rhs[index].special)
            return false;
    }
    return true;
}

WORKSPACEID next_workspace_id(const ScrollerCanvasSnapshot::RepositorySnapshot& snapshot,
                              const std::vector<SyntheticCanvasWorkspace>& synthetics = {}) {
    WORKSPACEID maxWorkspaceId = 0;
    if (g_pCompositor) {
        for (const auto& workspaceRef : ScrollerCore::HyprlandRuntime::workspaces()) {
            const auto workspace = workspaceRef.lock();
            if (!workspace)
                continue;
            maxWorkspaceId = std::max(maxWorkspaceId, workspace->m_id);
        }
    }
    for (const auto& canvas : snapshot.canvases) {
        for (const auto& member : canvas.members)
            maxWorkspaceId = std::max(maxWorkspaceId, static_cast<WORKSPACEID>(member.workspaceId));
    }
    for (const auto& synthetic : synthetics) {
        for (const auto& member : synthetic.canvas.members)
            maxWorkspaceId = std::max(maxWorkspaceId, member.workspaceId);
    }
    return maxWorkspaceId + 1;
}

int next_canvas_id(const ScrollerCanvasSnapshot::RepositorySnapshot& snapshot,
                   const std::vector<SyntheticCanvasWorkspace>& synthetics = {}) {
    auto nextId = 0;
    for (const auto& canvas : snapshot.canvases)
        nextId = std::max(nextId, canvas.canvasId + 1);
    for (const auto& synthetic : synthetics)
        nextId = std::max(nextId, synthetic.canvas.canvasId + 1);
    return nextId;
}

std::vector<CanvasWorkspaceMember> blank_members_for_visible_monitors(const ScrollerCanvasSnapshot::RepositorySnapshot& snapshot,
                                                                     const std::vector<SyntheticCanvasWorkspace>& synthetics = {}) {
    std::vector<CanvasWorkspaceMember> members;
    if (!g_pCompositor)
        return members;

    auto workspaceId = next_workspace_id(snapshot, synthetics);
    for (const auto& monitor : ScrollerCore::HyprlandRuntime::monitors()) {
        if (!monitor)
            continue;
        members.push_back({
            .monitorId = static_cast<int>(monitor->m_id),
            .workspaceId = workspaceId++,
            .special = false,
        });
    }
    std::sort(members.begin(), members.end(), [](const auto& lhs, const auto& rhs) {
        return lhs.monitorId < rhs.monitorId;
    });
    return members;
}

void apply_preview_insertion(std::vector<CanvasWorkspaceRecord>& canvases, const SyntheticCanvasWorkspace& synthetic) {
    const auto* anchor = find_canvas(canvases, synthetic.anchorCanvasId);
    if (!anchor) {
        canvases.push_back(synthetic.canvas);
        return;
    }

    const auto shiftMatching = [&](auto&& predicate, int dx, int dy) {
        for (auto& canvas : canvases) {
            if (predicate(canvas)) {
                canvas.tileX += dx;
                canvas.tileY += dy;
            }
        }
    };

    const auto targetX = synthetic.canvas.tileX;
    const auto targetY = synthetic.canvas.tileY;
    switch (synthetic.direction) {
        case Direction::Left:
            shiftMatching([&](const auto& canvas) {
                return canvas.tileY == anchor->tileY && canvas.tileX <= targetX;
            }, -1, 0);
            break;
        case Direction::Right:
            shiftMatching([&](const auto& canvas) {
                return canvas.tileY == anchor->tileY && canvas.tileX >= targetX;
            }, 1, 0);
            break;
        case Direction::Up:
            shiftMatching([&](const auto& canvas) {
                return canvas.tileX == anchor->tileX && canvas.tileY <= targetY;
            }, 0, -1);
            break;
        case Direction::Down:
            shiftMatching([&](const auto& canvas) {
                return canvas.tileX == anchor->tileX && canvas.tileY >= targetY;
            }, 0, 1);
            break;
        default:
            break;
    }

    canvases.push_back(synthetic.canvas);
}

void apply_preview_insertions(std::vector<CanvasWorkspaceRecord>& canvases, const std::vector<SyntheticCanvasWorkspace>& synthetics) {
    for (const auto& synthetic : synthetics)
        apply_preview_insertion(canvases, synthetic);
}

} // namespace

CanvasWorkspaceRepository& canvasRepository() {
    static CanvasWorkspaceRepository repo;
    return repo;
}

void CanvasWorkspaceRepository::initialize() {
    if (initialized_)
        return;

    initialized_ = true;
    loadFromDisk();
}

void CanvasWorkspaceRepository::flush() const {
    if (!initialized_)
        return;
    flushToDisk();
}

int CanvasWorkspaceRepository::activeCanvasId() const {
    return snapshot_.activeCanvasId;
}

std::vector<CanvasWorkspaceRecord> CanvasWorkspaceRepository::canvases() const {
    std::vector<CanvasWorkspaceRecord> records;
    records.reserve(snapshot_.canvases.size());
    for (const auto& canvas : snapshot_.canvases)
        records.push_back(from_snapshot(canvas));
    return records;
}

std::optional<CanvasWorkspaceRecord> CanvasWorkspaceRepository::find(int canvasId) const {
    for (const auto& canvas : snapshot_.canvases) {
        if (canvas.canvasId == canvasId)
            return from_snapshot(canvas);
    }
    return std::nullopt;
}

std::vector<CanvasWorkspaceMember> CanvasWorkspaceRepository::currentVisibleMembers() const {
    std::vector<CanvasWorkspaceMember> members;
    if (!g_pCompositor)
        return members;

    for (const auto& monitor : ScrollerCore::HyprlandRuntime::monitors()) {
        if (!monitor)
            continue;

        const auto workspaceId = CanvasLayoutInternal::preferred_workspace_id(monitor, monitor->activeWorkspaceID());
        const auto workspace = ScrollerCore::HyprlandRuntime::workspaceById(workspaceId);
        members.push_back({
            .monitorId = static_cast<int>(monitor->m_id),
            .workspaceId = workspaceId,
            .special = workspace ? workspace->m_isSpecialWorkspace : false,
        });
    }

    std::sort(members.begin(), members.end(), [](const auto& lhs, const auto& rhs) {
        return lhs.monitorId < rhs.monitorId;
    });
    return members;
}

int CanvasWorkspaceRepository::ensureCurrentVisibleCanvas() {
    initialize();
    const auto visible = currentVisibleMembers();
    if (visible.empty())
        return -1;

    for (const auto& canvas : snapshot_.canvases) {
        if (members_match(from_snapshot(canvas).members, visible)) {
            snapshot_.activeCanvasId = canvas.canvasId;
            flushToDisk();
            return canvas.canvasId;
        }
    }

    auto records = canvases();
    const auto* active = find_canvas(records, snapshot_.activeCanvasId);
    const auto nextId = next_canvas_id(snapshot_);
    const auto tileX = active ? active->tileX + 1 : (records.empty() ? 0 : std::max_element(records.begin(), records.end(),
        [](const auto& lhs, const auto& rhs) {
            return lhs.tileX < rhs.tileX;
        })->tileX + 1);
    const auto tileY = active ? active->tileY : 0;

    snapshot_.canvases.push_back(to_snapshot({
        .canvasId = nextId,
        .tileX = tileX,
        .tileY = tileY,
        .members = visible,
    }));
    snapshot_.activeCanvasId = nextId;
    flushToDisk();
    return nextId;
}

SyntheticCanvasWorkspace CanvasWorkspaceRepository::buildSyntheticCanvas(Direction direction,
                                                                         int anchorCanvasId,
                                                                         const std::vector<SyntheticCanvasWorkspace>& existingSynthetics) const {
    auto records = previewCanvases(existingSynthetics);
    const auto* anchor = find_canvas(records, anchorCanvasId);
    if (!anchor)
        anchor = find_canvas(records, snapshot_.activeCanvasId);
    if (!anchor && !records.empty())
        anchor = &records.front();

    auto synthetic = SyntheticCanvasWorkspace{};
    synthetic.anchorCanvasId = anchor ? anchor->canvasId : -1;
    synthetic.direction = direction;
    synthetic.canvas.canvasId = next_canvas_id(snapshot_, existingSynthetics);
    synthetic.canvas.members = blank_members_for_visible_monitors(snapshot_, existingSynthetics);
    synthetic.canvas.tileX = anchor ? anchor->tileX : 0;
    synthetic.canvas.tileY = anchor ? anchor->tileY : 0;
    switch (direction) {
        case Direction::Left:
            synthetic.canvas.tileX -= 1;
            break;
        case Direction::Right:
            synthetic.canvas.tileX += 1;
            break;
        case Direction::Up:
            synthetic.canvas.tileY -= 1;
            break;
        case Direction::Down:
            synthetic.canvas.tileY += 1;
            break;
        default:
            break;
    }
    return synthetic;
}

std::vector<CanvasWorkspaceRecord> CanvasWorkspaceRepository::previewCanvases(const std::vector<SyntheticCanvasWorkspace>& synthetics) const {
    auto records = canvases();
    apply_preview_insertions(records, synthetics);
    return records;
}

void CanvasWorkspaceRepository::commitSyntheticCanvases(const std::vector<SyntheticCanvasWorkspace>& synthetics, int activeCanvasId) {
    initialize();
    auto records = previewCanvases(synthetics);
    snapshot_.canvases.clear();
    snapshot_.canvases.reserve(records.size());
    for (const auto& canvas : records)
        snapshot_.canvases.push_back(to_snapshot(canvas));
    snapshot_.activeCanvasId = activeCanvasId;
    flushToDisk();
}

void CanvasWorkspaceRepository::markActive(int canvasId) {
    initialize();
    snapshot_.activeCanvasId = canvasId;
    flushToDisk();
}

void CanvasWorkspaceRepository::ensureCanvasHasVisibleMembers(int canvasId) {
    initialize();
    auto records = canvases();
    auto* canvas = find_canvas(records, canvasId);
    if (!canvas)
        return;

    auto nextWorkspace = next_workspace_id(snapshot_);
    bool changed = false;
    for (const auto& visibleMember : currentVisibleMembers()) {
        const auto exists = std::any_of(canvas->members.begin(), canvas->members.end(), [&](const auto& member) {
            return member.monitorId == visibleMember.monitorId;
        });
        if (exists)
            continue;

        canvas->members.push_back({
            .monitorId = visibleMember.monitorId,
            .workspaceId = nextWorkspace++,
            .special = false,
        });
        changed = true;
    }

    if (!changed)
        return;

    std::sort(canvas->members.begin(), canvas->members.end(), [](const auto& lhs, const auto& rhs) {
        return lhs.monitorId < rhs.monitorId;
    });
    snapshot_.canvases.clear();
    snapshot_.canvases.reserve(records.size());
    for (const auto& record : records)
        snapshot_.canvases.push_back(to_snapshot(record));
    flushToDisk();
}

std::string CanvasWorkspaceRepository::repositoryPath() const {
    return runtime_base_dir() + "/hyprscroller-canvas-" + hyprland_instance_signature() + ".state";
}

void CanvasWorkspaceRepository::loadFromDisk() {
    snapshot_ = {};

    std::ifstream input(repositoryPath());
    if (!input.is_open())
        return;

    const std::string data((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    const auto parsed = ScrollerCanvasSnapshot::deserialize_repository(data);
    if (!parsed) {
        spdlog::warn("canvas_repository: failed to parse snapshot file path={}", repositoryPath());
        return;
    }

    snapshot_ = *parsed;
    spdlog::info("canvas_repository: loaded canvas_count={} path={}", snapshot_.canvases.size(), repositoryPath());
}

void CanvasWorkspaceRepository::flushToDisk() const {
    namespace fs = std::filesystem;

    const auto path = repositoryPath();
    const fs::path filePath(path);
    const auto directory = filePath.parent_path();
    if (!directory.empty()) {
        std::error_code ec;
        fs::create_directories(directory, ec);
        if (ec) {
            spdlog::warn("canvas_repository: failed to create directory path={} error={}", directory.string(), ec.message());
            return;
        }
    }

    const auto tempPath = path + ".tmp";
    std::ofstream output(tempPath, std::ios::trunc);
    if (!output.is_open()) {
        spdlog::warn("canvas_repository: failed to open temp file path={} errno={}", tempPath, errno);
        return;
    }

    output << ScrollerCanvasSnapshot::serialize_repository(snapshot_);
    output.close();

    std::error_code ec;
    fs::rename(tempPath, path, ec);
    if (ec) {
        spdlog::warn("canvas_repository: failed to replace path={} error={}", path, ec.message());
        fs::remove(tempPath, ec);
    }
}

} // namespace CanvasLayoutState
