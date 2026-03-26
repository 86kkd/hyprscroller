/**
 * @file main.cpp
 * @brief Plugin entrypoints, config registration, and logging bootstrap.
 *
 * This file is intentionally small: it wires Hyprland's plugin ABI to the
 * layout implementation, registers plugin config values and dispatchers, and
 * initializes the dedicated file logger used for debugging layout behavior.
 */
#include <hyprland/src/config/ConfigManager.hpp>
#include <hyprland/src/plugins/PluginAPI.hpp>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <typeinfo>

#include "dispatchers.h"
#include "hyprlang.hpp"
#include "layout/canvas/layout.h"
#include "overview/render.h"

// Hyprland plugin handle used by config lookups and dispatcher registration.
HANDLE PHANDLE = nullptr;

namespace {
// Resolve the dedicated log file used by the plugin across sessions.
std::string log_file_path() {
    const char* home = std::getenv("HOME");
    const auto base = home ? std::filesystem::path(home) : std::filesystem::path("/tmp");
    return (base / ".hyprland/plugins/hyprscroller/hyprscroller.log").string();
}

// Initialize the file-backed spdlog logger used by all plugin code.
void init_logging() {
    const auto path = log_file_path();
    std::filesystem::create_directories(std::filesystem::path(path).parent_path());

    spdlog::drop("hyprscroller");
    auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(path, true);
    auto logger = std::make_shared<spdlog::logger>("hyprscroller", std::move(sink));
    spdlog::set_default_logger(std::move(logger));
    spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [hyprscroller] [%^%l%$] %v");
#ifndef NDEBUG
    spdlog::set_level(spdlog::level::debug);
#else
    spdlog::set_level(spdlog::level::info);
#endif
    spdlog::flush_on(spdlog::level::debug);
    spdlog::info("logging initialized path={}", path);
}
} // namespace

// Report the Hyprland plugin API version this build targets. Hyprland queries
// this symbol before accepting the plugin.
APICALL EXPORT std::string PLUGIN_API_VERSION() {
    return HYPRLAND_API_VERSION;
}

// Register config values, dispatchers, and the tiled algorithm implementation.
// The returned description tuple is shown by Hyprland/plugin tooling as:
// { name, description, author, version }.
APICALL EXPORT PLUGIN_DESCRIPTION_INFO PLUGIN_INIT(HANDLE handle) {
    PHANDLE = handle;
    init_logging();
    spdlog::info("pluginInit handle={}", static_cast<const void*>(handle));

#ifdef COLORS_IPC
    // Enable optional IPC color configuration for free-stack highlight.
    HyprlandAPI::addConfigValue(PHANDLE, "plugin:scroller:col.freecolumn_border", Hyprlang::CConfigValue(Hyprlang::INT(0xff9e1515)));
#endif

    // one value out of: { onethird, onehalf (default), twothirds, floating, maximized }
    HyprlandAPI::addConfigValue(PHANDLE, "plugin:scroller:column_default_width", Hyprlang::STRING{"onehalf"});
    // 0, 1
    HyprlandAPI::addConfigValue(PHANDLE, "plugin:scroller:focus_wrap", Hyprlang::INT{0});

    // Register custom dispatchers used by keybinds and user scripts.
    dispatchers::addDispatchers();

    // Register scroller as a custom tiled algorithm only after all config values
    // it may read during initial workspace population have been registered.
    HyprlandAPI::addTiledAlgo(
        PHANDLE,
        "scroller",
        &typeid(CanvasLayout),
        []() -> UP<Layout::ITiledAlgorithm> { return makeUnique<CanvasLayout>(); });

    Overview::initializeRendererHooks(PHANDLE);

    // Keep the exported plugin metadata stable for plugin discovery and UI.
    return {"hyprscroller", "scrolling window layout", "dawser", "1.0"};
}

// Plugin shutdown hook used for final logging only.
APICALL EXPORT void PLUGIN_EXIT() {
    Overview::shutdownRendererHooks(PHANDLE);
    spdlog::info("pluginExit");
}
