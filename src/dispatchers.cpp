/**
 * @file dispatchers.cpp
 * @brief Dispatcher bootstrap that wires layout and overview command groups.
 */
#include "dispatchers.h"
#include "dispatchers_internal.h"

// Register all plugin dispatchers into Hyprland's dispatcher map.
void dispatchers::addDispatchers() {
    detail::registerLayoutDispatchers();
    detail::registerOverviewMarkDispatchers();
}
