#include <hyprland/src/config/ConfigManager.hpp>
#include <hyprland/src/plugins/PluginAPI.hpp>
#include <spdlog/spdlog.h>
#include <typeinfo>

#include "dispatchers.h"
#include "hyprlang.hpp"
#include "layout/scroller_layout.h"

HANDLE PHANDLE = nullptr;

APICALL EXPORT std::string PLUGIN_API_VERSION() {
    return HYPRLAND_API_VERSION;
}

APICALL EXPORT PLUGIN_DESCRIPTION_INFO PLUGIN_INIT(HANDLE handle) {
    PHANDLE = handle;

    spdlog::set_pattern("[hyprscroller] [%^%l%$] %v");
#ifndef NDEBUG
    spdlog::set_level(spdlog::level::debug);
#else
    spdlog::set_level(spdlog::level::info);
#endif
    spdlog::info("pluginInit handle={}", static_cast<const void*>(handle));

#ifdef COLORS_IPC
    // Enable optional IPC color configuration for free-column highlight.
    HyprlandAPI::addConfigValue(PHANDLE, "plugin:scroller:col.freecolumn_border", Hyprlang::CConfigValue(Hyprlang::INT(0xff9e1515)));
#endif

    // Register scroller as a custom tiled algorithm. Hyprland will instantiate
    // ScrollerLayout for matching layout names.
    HyprlandAPI::addTiledAlgo(
        PHANDLE,
        "scroller",
        &typeid(ScrollerLayout),
        []() -> UP<Layout::ITiledAlgorithm> { return makeUnique<ScrollerLayout>(); });

    // Register custom dispatchers used by keybinds and user scripts.
    dispatchers::addDispatchers();

    // one value out of: { onethird, onehalf (default), twothirds, floating, maximized }
    HyprlandAPI::addConfigValue(PHANDLE, "plugin:scroller:column_default_width", Hyprlang::STRING{"onehalf"});
    // 0, 1
    HyprlandAPI::addConfigValue(PHANDLE, "plugin:scroller:focus_wrap", Hyprlang::INT{1});

    return {"hyprscroller", "scrolling window layout", "dawser", "1.0"};
}

APICALL EXPORT void PLUGIN_EXIT() {
    spdlog::info("pluginExit");
}
