#pragma once

#include <cstdint>

#include <hyprland/src/desktop/view/window/Window.hpp>
#include <hyprland/src/desktop/view/window/WindowMetadata.hpp>

namespace ScrollerCore {

// Use Hyprland's lifetime-stable window identity. Pointer addresses can be reused
// after a view closes, which corrupts persisted layouts across plugin reloads.
inline uintptr_t window_key(PHLWINDOW window) {
    return window ? static_cast<uintptr_t>(window->metadata().stableID()) : 0;
}

// Snapshot formats through v4 persisted the CWindow address. Keep this helper
// only for loading those files during a rolling plugin upgrade.
inline uintptr_t legacy_window_key(PHLWINDOW window) {
    return window ? reinterpret_cast<uintptr_t>(window.get()) : 0;
}

inline uintptr_t snapshot_window_key(PHLWINDOW window, int snapshotVersion, int stableKeyVersion) {
    return snapshotVersion >= stableKeyVersion ? window_key(window) : legacy_window_key(window);
}

} // namespace ScrollerCore
