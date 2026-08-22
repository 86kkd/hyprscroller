/**
 * @file plugin/dispatch/overview_mark_commands.cpp
 * @brief Overview and mark dispatcher entrypoints and registration.
 */
#include "plugin/dispatch/shared.h"

#include <lua.hpp>

namespace dispatchers::detail {
namespace {

// toggleoverview: enter or accept the global logical overview session.
void dispatch_toggleoverview(std::string arg) {
    auto& overview = overviewSessionForDispatch();

    if (isOverviewCancelArg(arg)) {
        if (overview.active())
            overview.close(false);
        return;
    }

    if (isOverviewAcceptArg(arg)) {
        if (overview.active())
            overview.close(true);
        else
            overview.open();
        return;
    }

    if (overview.active()) {
        overview.close(true);
        return;
    }

    overview.open();
}

// canceloverview: leave the global overview session without accepting.
void dispatch_canceloverview(std::string arg) {
    (void)arg;
    closeOverviewIfActive(false);
}

// marksadd <name>: save focused window under a named mark.
void dispatch_marksadd(std::string arg) {
    withWorkspaceLayout([&](CanvasLayout& layout, int workspace) {
        (void)workspace;
        layout.marks_add(arg);
    });
}

// marksdelete <name>: remove a stored mark entry by name.
void dispatch_marksdelete(std::string arg) {
    withLayout([&](CanvasLayout& layout) {
        layout.marks_delete(arg);
    });
}

// marksvisit <name>: focus and activate the marked window if present.
void dispatch_marksvisit(std::string arg) {
    withLayout([&](CanvasLayout& layout) {
        layout.marks_visit(arg);
    });
}

// marksreset: clear all marks.
void dispatch_marksreset(std::string arg) {
    (void)arg;
    withLayout([&](CanvasLayout& layout) {
        layout.marks_reset();
    });
}

} // namespace

void registerOverviewMarkDispatchers() {
    registerDispatcher("scroller:toggleoverview", dispatch_toggleoverview);
    registerDispatcher("scroller:canceloverview", dispatch_canceloverview);
    registerDispatcher("scroller:marksadd", dispatch_marksadd);
    registerDispatcher("scroller:marksdelete", dispatch_marksdelete);
    registerDispatcher("scroller:marksvisit", dispatch_marksvisit);
    registerDispatcher("scroller:marksreset", dispatch_marksreset);
}

namespace {

std::string luaStringArg(lua_State* state) {
    if (lua_gettop(state) < 1 || !lua_isstring(state, 1))
        return {};

    size_t length = 0;
    const char* value = lua_tolstring(state, 1, &length);
    return value ? std::string(value, length) : std::string();
}

int lua_toggle_overview(lua_State* state) {
    dispatch_toggleoverview(luaStringArg(state));
    return 0;
}

int lua_cancel_overview(lua_State* state) {
    dispatch_canceloverview(luaStringArg(state));
    return 0;
}

int lua_marks_add(lua_State* state) {
    dispatch_marksadd(luaStringArg(state));
    return 0;
}

int lua_marks_delete(lua_State* state) {
    dispatch_marksdelete(luaStringArg(state));
    return 0;
}

int lua_marks_visit(lua_State* state) {
    dispatch_marksvisit(luaStringArg(state));
    return 0;
}

int lua_marks_reset(lua_State* state) {
    dispatch_marksreset(luaStringArg(state));
    return 0;
}

void registerLuaFunction(const char* name, PLUGIN_LUA_FN function) {
    if (!HyprlandAPI::addLuaFunction(PHANDLE, "scroller", name, function))
        spdlog::warn("failed to register Lua function hl.plugin.scroller.{}", name);
}

} // namespace

void registerOverviewMarkLuaFunctions() {
    registerLuaFunction("toggle_overview", lua_toggle_overview);
    registerLuaFunction("cancel_overview", lua_cancel_overview);
    registerLuaFunction("marks_add", lua_marks_add);
    registerLuaFunction("marks_delete", lua_marks_delete);
    registerLuaFunction("marks_visit", lua_marks_visit);
    registerLuaFunction("marks_reset", lua_marks_reset);
}

} // namespace dispatchers::detail
