#pragma once

#include <cstdint>

#include <hyprland/src/desktop/view/Window.hpp>

namespace ScrollerCore {

// Convert a compositor window handle into the stable integer key used by owner caches.
inline uintptr_t window_key(PHLWINDOW window) {
    return window ? reinterpret_cast<uintptr_t>(window.get()) : 0;
}

} // namespace ScrollerCore
