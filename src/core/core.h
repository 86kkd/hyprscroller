/**
 * @file core.h
 * @brief Shared geometry/model utilities used by scroller modules.
 *
 * Centralized helpers for window/monitor adaptation, geometry utilities, and
 * mark storage are kept here so layout and model layers share the same
 * low-level behavior.
 */
#pragma once

#include <string>
#include <unordered_map>

#include <hyprland/src/helpers/Monitor.hpp>
#include <hyprland/src/layout/target/Target.hpp>
#include <hyprland/src/desktop/view/Window.hpp>

namespace ScrollerCore {

// Convert the generic tiled target used by Hyprland into a concrete window pointer.
PHLWINDOW windowFromTarget(SP<Layout::ITarget> target);

// Resolve workspace/monitor context from pointer position used by keyboard actions.
PHLMONITOR monitorFromPointingOrCursor();

// Cross-workspace bookmark table used by marks:* dispatchers.
class Marks {
public:
    Marks() {}
    ~Marks() { reset(); }

    // Remove all marks.
    void reset();
    // Add/replace a mark for the given name.
    void add(PHLWINDOW window, const std::string &name);
    // Delete a mark by name.
    void del(const std::string &name);
    // Remove all marks that refer to a dying window.
    void remove(PHLWINDOW window);
    // Resolve a name to window pointer, or null if missing.
    PHLWINDOW visit(const std::string &name);

private:
    std::unordered_map<std::string, PHLWINDOWREF> marks;
};

} // namespace ScrollerCore
