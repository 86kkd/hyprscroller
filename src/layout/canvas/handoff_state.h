#pragma once

#include <cstdint>
#include <unordered_set>
#include <utility>

enum class ActiveLaneSyncPolicy { None, WorkspaceFocus };

struct HandoffState {
    bool suppressWorkspaceFocusSync = false;
    std::unordered_set<uintptr_t> pendingManualCrossMonitorInsertions;

    void requestWorkspaceFocusSyncSuppression() {
        suppressWorkspaceFocusSync = true;
    }

    ActiveLaneSyncPolicy consumeActiveLaneSyncPolicy() {
        return std::exchange(suppressWorkspaceFocusSync, false)
            ? ActiveLaneSyncPolicy::None
            : ActiveLaneSyncPolicy::WorkspaceFocus;
    }

    void rememberManualCrossMonitorInsertion(uintptr_t windowKey) {
        pendingManualCrossMonitorInsertions.insert(windowKey);
    }

    void forgetManualCrossMonitorInsertion(uintptr_t windowKey) {
        pendingManualCrossMonitorInsertions.erase(windowKey);
    }

    bool hasPendingManualCrossMonitorInsertion(uintptr_t windowKey) const {
        return pendingManualCrossMonitorInsertions.contains(windowKey);
    }

    void reset() {
        suppressWorkspaceFocusSync = false;
        pendingManualCrossMonitorInsertions.clear();
    }
};
