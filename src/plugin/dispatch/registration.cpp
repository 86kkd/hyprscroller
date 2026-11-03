/**
 * @file plugin/dispatch/registration.cpp
 * @brief Dispatcher bootstrap that wires layout and overview command groups.
 */
#include "plugin/dispatch/registration.h"
#include "plugin/dispatch/shared.h"

void dispatchers::addLuaFunctions() {
    detail::registerLayoutLuaFunctions();
    detail::registerOverviewMarkLuaFunctions();
}
