#include "layout_repository.h"

#include <cerrno>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <spdlog/spdlog.h>
#include <string>
#include <system_error>

namespace CanvasLayoutState {
namespace {

std::string runtime_base_dir() {
    if (const auto *xdgRuntime = std::getenv("XDG_RUNTIME_DIR"); xdgRuntime && xdgRuntime[0] != '\0')
        return xdgRuntime;
    return "/tmp";
}

std::string hyprland_instance_signature() {
    if (const auto *signature = std::getenv("HYPRLAND_INSTANCE_SIGNATURE"); signature && signature[0] != '\0')
        return signature;
    return "default";
}

} // namespace

WorkspaceLayoutRepository &repository() {
    static WorkspaceLayoutRepository repo;
    return repo;
}

void WorkspaceLayoutRepository::initialize() {
    if (initialized_)
        return;

    initialized_ = true;
    loadFromDisk();
}

std::optional<ScrollerSnapshot::CanvasSnapshot> WorkspaceLayoutRepository::find(int workspaceId) const {
    const auto it = snapshots_.find(workspaceId);
    if (it == snapshots_.end())
        return std::nullopt;
    return it->second;
}

void WorkspaceLayoutRepository::upsert(ScrollerSnapshot::CanvasSnapshot snapshot) {
    initialize();
    snapshots_[snapshot.workspaceId] = std::move(snapshot);
    flushToDisk();
}

void WorkspaceLayoutRepository::erase(int workspaceId) {
    initialize();
    if (snapshots_.erase(workspaceId) == 0)
        return;
    flushToDisk();
}

void WorkspaceLayoutRepository::flush() const {
    if (!initialized_)
        return;
    flushToDisk();
}

std::string WorkspaceLayoutRepository::repositoryPath() const {
    return runtime_base_dir() + "/hyprscroller-layout-" + hyprland_instance_signature() + ".state";
}

void WorkspaceLayoutRepository::loadFromDisk() {
    snapshots_.clear();

    std::ifstream input(repositoryPath());
    if (!input.is_open())
        return;

    const std::string data((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    const auto parsed = ScrollerSnapshot::deserialize_repository(data);
    if (!parsed) {
        spdlog::warn("layout_repository: failed to parse snapshot file path={}", repositoryPath());
        return;
    }

    snapshots_ = *parsed;
    spdlog::info("layout_repository: loaded workspace_count={} path={}", snapshots_.size(), repositoryPath());
}

void WorkspaceLayoutRepository::flushToDisk() const {
    namespace fs = std::filesystem;

    const auto path = repositoryPath();
    const fs::path filePath(path);
    const auto directory = filePath.parent_path();
    if (!directory.empty()) {
        std::error_code ec;
        fs::create_directories(directory, ec);
        if (ec) {
            spdlog::warn("layout_repository: failed to create directory path={} error={}", directory.string(), ec.message());
            return;
        }
    }

    const auto tempPath = path + ".tmp";
    std::ofstream output(tempPath, std::ios::trunc);
    if (!output.is_open()) {
        spdlog::warn("layout_repository: failed to open temp file path={} errno={}", tempPath, errno);
        return;
    }

    output << ScrollerSnapshot::serialize_repository(snapshots_);
    output.close();

    std::error_code ec;
    fs::rename(tempPath, path, ec);
    if (ec) {
        spdlog::warn("layout_repository: failed to replace path={} error={}", path, ec.message());
        fs::remove(tempPath, ec);
        return;
    }
}

} // namespace CanvasLayoutState
