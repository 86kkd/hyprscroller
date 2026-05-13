#pragma once

#include <string>

#include <hyprland/src/config/shared/complex/ComplexDataTypes.hpp>
#include <hyprland/src/config/values/ConfigValues.hpp>
#include <hyprland/src/plugins/PluginAPI.hpp>

namespace scroller::plugin_config {

inline SP<Config::Values::CStringValue> columnDefaultWidthValue;
inline SP<Config::Values::CIntValue>    focusWrapValue;
#ifdef COLORS_IPC
inline SP<Config::Values::CColorValue>  freeColumnBorderValue;
#endif

inline void registerConfigValues(HANDLE handle) {
    columnDefaultWidthValue = Config::Values::makeConfigValue<Config::Values::CStringValue>(
        "plugin:scroller:column_default_width",
        "Default size preset for newly created scroller columns.",
        Config::STRING{"onehalf"},
        {});
    focusWrapValue = Config::Values::makeConfigValue<Config::Values::CIntValue>(
        "plugin:scroller:focus_wrap",
        "Whether scroller focus wraps at lane and stack edges.",
        Config::INTEGER{0},
        Config::Values::SIntValueOptions{
            .min = Config::INTEGER{0},
            .max = Config::INTEGER{1},
        });

    HyprlandAPI::addConfigValueV2(handle, columnDefaultWidthValue);
    HyprlandAPI::addConfigValueV2(handle, focusWrapValue);

#ifdef COLORS_IPC
    freeColumnBorderValue = Config::Values::makeConfigValue<Config::Values::CColorValue>(
        "plugin:scroller:col.freecolumn_border",
        "Border color for free-sized scroller columns.",
        Config::INTEGER{0xff9e1515},
        {});
    HyprlandAPI::addConfigValueV2(handle, freeColumnBorderValue);
#endif
}

inline std::string columnDefaultWidth() {
    return columnDefaultWidthValue ? columnDefaultWidthValue->value() : "onehalf";
}

inline bool focusWrap() {
    return focusWrapValue && focusWrapValue->value() != 0;
}

#ifdef COLORS_IPC
inline Config::CGradientValueData freeColumnBorder() {
    return Config::CGradientValueData(CHyprColor(freeColumnBorderValue ? freeColumnBorderValue->value() : 0xff9e1515));
}
#endif

} // namespace scroller::plugin_config
