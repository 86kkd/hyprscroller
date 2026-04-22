/**
 * @file plugin/dispatch/registration.cpp
 * @brief Dispatcher bootstrap that wires layout and overview command groups.
 */
#include "plugin/dispatch/registration.h"
#include "plugin/dispatch/shared.h"

// Register all plugin dispatchers into Hyprland's dispatcher map.
void dispatchers::addDispatchers() {
    detail::registerLayoutDispatchers();
    detail::registerOverviewMarkDispatchers();
}
