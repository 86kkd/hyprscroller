/**
 * @file plugin/entry.cpp
 * @brief Plugin entrypoints, config registration, and logging bootstrap.
 *
 * This file is intentionally small: it wires Hyprland's plugin ABI to the
 * layout implementation, registers plugin config values and dispatchers, and
 * initializes the dedicated file logger used for debugging layout behavior.
 *
 * Newcomer reading guide:
 * 1. Start at `PLUGIN_INIT` in this file.
 * 2. Follow dispatcher registration into `src/plugin/dispatch/registration.cpp`.
 * 3. Follow tiled-algorithm registration into `CanvasLayout` in
 *    `src/layout/canvas/layout.h`.
 * That path shows the whole runtime shell before you dive into lane or stack
 * details.
 */
#include <hyprland/src/config/ConfigManager.hpp>
#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/plugins/PluginAPI.hpp>
#include <cerrno>
#include <cstring>
#include <cstdlib>
#include <memory>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <string>
#include <stdexcept>
#include <sys/stat.h>
#include <typeinfo>

#include "plugin/dispatch/registration.h"
#include "hyprlang.hpp"
#include "layout/canvas/internal.h"
#include "layout/canvas/layout.h"
#include "layout/canvas/layout_repository.h"
#include "layout/canvas/canvas_workspace_repository.h"
#include "overview/render/render.h"

// Hyprland plugin handle used by config lookups and dispatcher registration.
HANDLE PHANDLE = nullptr;

namespace {
// Resolve the dedicated log file used by the plugin across sessions.
std::string log_file_path() {
    const char* home = std::getenv("HOME");
    const std::string base = home && home[0] != '\0' ? home : "/tmp";
    return base + "/.hyprland/plugins/hyprscroller/hyprscroller.log";
}

bool ensure_directory(const std::string& path) {
    if (path.empty())
        return false;

    std::string current;
    current.reserve(path.size());

    for (size_t i = 0; i < path.size(); ++i) {
        current.push_back(path[i]);
        if (path[i] != '/' || current.size() == 1)
            continue;

        if (::mkdir(current.c_str(), 0755) == 0 || errno == EEXIST)
            continue;

        return false;
    }

    return ::mkdir(path.c_str(), 0755) == 0 || errno == EEXIST;
}

std::shared_ptr<spdlog::logger> make_stderr_logger() {
    auto sink = std::make_shared<spdlog::sinks::stderr_color_sink_mt>();
    return std::make_shared<spdlog::logger>("hyprscroller", std::move(sink));
}

// Initialize the plugin logger without letting filesystem setup abort plugin init.
void init_logging() {
    const auto path = log_file_path();
    const auto split = path.find_last_of('/');
    const auto directory = split == std::string::npos ? std::string() : path.substr(0, split);

    spdlog::drop("hyprscroller");

    std::shared_ptr<spdlog::logger> logger;
    try {
        if (!directory.empty() && !ensure_directory(directory))
            throw std::runtime_error(std::string("failed to create log directory: ") + directory + " (" + std::strerror(errno) + ")");

        auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(path, true);
        logger = std::make_shared<spdlog::logger>("hyprscroller", std::move(sink));
    } catch (const std::exception& e) {
        logger = make_stderr_logger();
        spdlog::set_default_logger(logger);
        spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [hyprscroller] [%^%l%$] %v");
        spdlog::set_level(spdlog::level::debug);
        spdlog::error("logging fallback to stderr: {}", e.what());
        return;
    }

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
// For code readers, this is the root of the plugin's runtime graph:
// Hyprland loads the plugin -> `PLUGIN_INIT` runs -> dispatchers are registered
// -> `CanvasLayout` is registered as the `scroller` tiled algorithm.
//
// The returned description tuple is shown by Hyprland/plugin tooling as:
// { name, description, author, version }.
APICALL EXPORT PLUGIN_DESCRIPTION_INFO PLUGIN_INIT(HANDLE handle) {
    PHANDLE = handle;
    init_logging();
    CanvasLayoutState::repository().initialize();
    CanvasLayoutState::canvasRepository().initialize();
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
    for (const auto &workspaceRef : g_pCompositor->getWorkspaces()) {
        const auto workspace = workspaceRef.lock();
        if (!workspace)
            continue;
        if (auto *layout = CanvasLayoutInternal::get_canvas_for_workspace(workspace->m_id))
            layout->persistCurrentSnapshot();
    }
    CanvasLayoutState::repository().flush();
    CanvasLayoutState::canvasRepository().flush();
    Overview::shutdownRendererHooks(PHANDLE);
    spdlog::info("pluginExit");
}
