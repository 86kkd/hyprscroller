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
find_package(spdlog REQUIRED)

# Compile flags
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED true)
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-gnu-unique")
endif()
add_library(hyprscroller SHARED
            src/main.cpp
            src/dispatchers.h
            src/dispatchers.cpp
            src/core/core.cpp
            src/core/direction.cpp
            src/core/layout_math.cpp
            src/core/layout_profile.cpp
            src/list.h
            src/overview/logic.h
            src/overview/logic.cpp
            src/overview/orientation_math.h
            src/overview/orientation_math.cpp
            src/overview/monitor_space.h
            src/overview/monitor_space.cpp
            src/overview/model.h
            src/overview/model.cpp
            src/overview/session.h
            src/overview/session.cpp
            src/overview/render.h
            src/overview/render.cpp
            src/overview/pass_element.h
            src/overview/pass_element.cpp
            src/model/window.cpp
            src/model/stack.cpp
            src/layout/canvas/core.cpp
            src/layout/canvas/overview.cpp
            src/layout/canvas/dispatch.cpp
            src/layout/canvas/dispatch_logic.cpp
            src/layout/canvas/route.cpp
            src/layout/canvas/route_logic.cpp
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

target_include_directories(hyprscroller PRIVATE ${DEPS_INCLUDE_DIRS} 
            /usr/include/hyprland/wlroots-hyprland/hyprland 
            /usr/include/hyprland/src
)
target_link_libraries(hyprscroller PRIVATE spdlog::spdlog_header_only)
set_target_properties(hyprscroller PROPERTIES PREFIX "")

if(BUILD_TESTING)
    add_executable(hyprscroller_logic_tests
                   tests/logic_tests.cpp
                   src/core/direction.cpp
                   src/core/layout_math.cpp
                   src/core/layout_profile.cpp
                   src/overview/logic.cpp
                   src/overview/orientation_math.cpp
                   src/layout/canvas/dispatch_logic.cpp
                   src/layout/canvas/route_logic.cpp)
    target_include_directories(hyprscroller_logic_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${DEPS_INCLUDE_DIRS}
        /usr/include/hyprland/wlroots-hyprland/hyprland
        /usr/include/hyprland/src)
    target_link_libraries(hyprscroller_logic_tests PRIVATE spdlog::spdlog_header_only stdc++ m)
    set_target_properties(hyprscroller_logic_tests PROPERTIES LINKER_LANGUAGE CXX)
    add_test(NAME hyprscroller.logic COMMAND hyprscroller_logic_tests)
endif()
