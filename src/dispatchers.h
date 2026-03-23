/**
 * @file dispatchers.h
 * @brief Dispatcher registration entrypoint for scroller layout commands.
 *
 * The plugin exposes its user-facing command surface through Hyprland
 * dispatchers. This header keeps the registration API intentionally narrow so
 * the rest of the plugin can treat dispatcher wiring as a single bootstrap
 * step.
 */
#pragma once

namespace dispatchers {
    // Register every `scroller:*` dispatcher into Hyprland's dispatcher table.
    void addDispatchers();
}
