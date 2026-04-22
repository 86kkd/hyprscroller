#pragma once

#include <optional>
#include <string>

#include "../../core/layout_snapshot.h"

namespace CanvasLayoutState {

class WorkspaceLayoutRepository {
public:
    void initialize();
    void flush() const;

    std::optional<ScrollerSnapshot::CanvasSnapshot> find(int workspaceId) const;
    void upsert(ScrollerSnapshot::CanvasSnapshot snapshot);
    void erase(int workspaceId);

private:
    std::string repositoryPath() const;
    void loadFromDisk();
    void flushToDisk() const;

    bool initialized_ = false;
    ScrollerSnapshot::RepositorySnapshot snapshots_;
};

WorkspaceLayoutRepository &repository();

} // namespace CanvasLayoutState
