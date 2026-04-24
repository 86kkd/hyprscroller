cmake_minimum_required(VERSION 3.12)
project("hyprscroller"
    LANGUAGES CXX
    DESCRIPTION "Hyprland layout similar to PaperWM and niri")

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
include(CTest)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

option(HYPRSCROLLER_ENABLE_SANITIZERS "Enable ASan/UBSan in debug builds" OFF)

find_package(PkgConfig REQUIRED)
pkg_check_modules(DEPS REQUIRED hyprland pixman-1 libdrm)
pkg_check_modules(HYPRLAND REQUIRED IMPORTED_TARGET hyprland)
find_package(spdlog REQUIRED)

# Compile flags
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED true)
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-gnu-unique")
endif()
add_library(hyprscroller SHARED
            src/plugin/entry.cpp
            src/plugin/dispatch/registration.h
            src/plugin/dispatch/shared.h
            src/plugin/dispatch/registration.cpp
            src/plugin/dispatch/layout_commands.cpp
            src/plugin/dispatch/overview_mark_commands.cpp
            src/core/core.cpp
            src/core/direction.cpp
            src/core/canvas_workspace_snapshot.cpp
            src/core/layout_snapshot.cpp
            src/core/layout_math.cpp
            src/core/monitor_geometry.cpp
            src/core/monitor_geometry_runtime.cpp
            src/core/layout_profile.cpp
            src/core/workspace_selector.h
            src/core/intrusive_list.h
            src/overview/navigation/logic.h
            src/overview/navigation/logic.cpp
            src/overview/scene/orientation.h
            src/overview/scene/orientation.cpp
            src/overview/scene/geometry_utils.h
            src/overview/render/animation.h
            src/overview/render/style.h
            src/overview/model/workspace_grid.h
            src/overview/model/workspace_grid.cpp
            src/overview/model/model.h
            src/overview/model/model.cpp
            src/overview/model/layout.h
            src/overview/model/layout.cpp
            src/overview/scene/scene.h
            src/overview/scene/scene.cpp
            src/overview/scene/layout.h
            src/overview/scene/layout.cpp
            src/overview/session/session.h
            src/overview/session/session.cpp
            src/overview/session/effects.h
            src/overview/session/effects.cpp
            src/overview/session/effects_runtime.h
            src/overview/session/effects_logic.cpp
            src/overview/navigation/selection.h
            src/overview/navigation/selection.cpp
            src/overview/render/render.h
            src/overview/render/render.cpp
            src/overview/render/state.h
            src/overview/render/state.cpp
            src/overview/render/draw.h
            src/overview/render/draw.cpp
            src/overview/render/hooks.h
            src/overview/render/hooks.cpp
            src/overview/render/pass_element.h
            src/overview/render/pass_element.cpp
            src/model/stack/window.cpp
            src/model/stack/core.cpp
            src/model/stack/membership.cpp
            src/model/stack/geometry.cpp
            src/layout/canvas/core.cpp
            src/layout/canvas/overview.cpp
            src/layout/canvas/dispatch.cpp
            src/layout/canvas/dispatch_runtime_logic.cpp
            src/layout/canvas/dispatch_logic.cpp
            src/layout/canvas/route.cpp
            src/layout/canvas/route_logic.cpp
            src/layout/canvas/layout_repository.cpp
            src/layout/canvas/canvas_workspace_repository.cpp
            src/layout/canvas/workspace.cpp
            src/layout/canvas/focus.cpp
            src/layout/canvas/commands.cpp
            src/layout/canvas/internal.h
            src/layout/lane/core.cpp
            src/layout/lane/actions.cpp
            src/layout/lane/geometry.cpp)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(hyprscroller PRIVATE
        $<$<CONFIG:Debug>:-Wall;-Wextra;-Wpedantic>)

    if(HYPRSCROLLER_ENABLE_SANITIZERS)
        target_compile_options(hyprscroller PRIVATE
            $<$<CONFIG:Debug>:-fsanitize=address,undefined;-fno-omit-frame-pointer>)
        set(CMAKE_SHARED_LINKER_FLAGS_DEBUG
            "${CMAKE_SHARED_LINKER_FLAGS_DEBUG} -fsanitize=address,undefined")
    endif()
endif()

add_compile_definitions(WLR_USE_UNSTABLE)

target_include_directories(hyprscroller PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/src
            ${DEPS_INCLUDE_DIRS}
            /usr/include/hyprland/wlroots-hyprland/hyprland 
            /usr/include/hyprland/src
)
target_link_libraries(hyprscroller PRIVATE spdlog::spdlog_header_only)
set_target_properties(hyprscroller PROPERTIES PREFIX "")

if(BUILD_TESTING)
    add_executable(hyprscroller_logic_tests
                   tests/test_main.cpp
                   tests/core_logic_tests.cpp
                   tests/layout_snapshot_tests.cpp
                   tests/layout_logic_tests.cpp
                   tests/overview_logic_tests.cpp
                   tests/overview_model_tests.cpp
                   tests/overview_scene_tests.cpp
                   tests/overview_session_logic_tests.cpp
                   tests/overview_session_effects_tests.cpp
                   src/core/direction.cpp
                   src/core/canvas_workspace_snapshot.cpp
                   src/core/layout_snapshot.cpp
                   src/core/layout_math.cpp
                   src/core/monitor_geometry.cpp
                   src/core/layout_profile.cpp
                   src/overview/navigation/logic.cpp
                   src/overview/scene/orientation.cpp
                   src/overview/model/workspace_grid.cpp
                   src/overview/scene/layout.cpp
                   src/overview/session/effects_logic.cpp
                   src/overview/navigation/selection.cpp
                   src/layout/canvas/dispatch_logic.cpp
                   src/layout/canvas/dispatch_runtime_logic.cpp
                   src/layout/canvas/route_logic.cpp)
    target_include_directories(hyprscroller_logic_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${DEPS_INCLUDE_DIRS}
        /usr/include/hyprland/wlroots-hyprland/hyprland
        /usr/include/hyprland/src)
    target_link_libraries(hyprscroller_logic_tests PRIVATE PkgConfig::HYPRLAND ${DEPS_LIBRARIES} spdlog::spdlog_header_only stdc++ m)
    set_target_properties(hyprscroller_logic_tests PROPERTIES LINKER_LANGUAGE CXX)
    add_test(NAME hyprscroller.logic COMMAND hyprscroller_logic_tests)
endif()
