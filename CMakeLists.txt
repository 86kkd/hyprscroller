cmake_minimum_required(VERSION 3.12)
project("hyprscroller"
    LANGUAGES CXX
    DESCRIPTION "Hyprland layout similar to PaperWM and niri")

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

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
            src/list.h
            src/model/model.cpp
            src/model/model_column.cpp
            src/layout/scroller_layout.cpp
            src/layout/scroller_layout_workspace.cpp
            src/layout/scroller_layout_focus.cpp
            src/layout/scroller_layout_internal.h
            src/layout/row.cpp
            src/layout/row_actions.cpp
            src/layout/row_geometry.cpp)

add_compile_definitions(WLR_USE_UNSTABLE)

target_include_directories(hyprscroller PRIVATE ${DEPS_INCLUDE_DIRS} 
            /usr/include/hyprland/wlroots-hyprland/hyprland 
            /usr/include/hyprland/src
)
target_link_libraries(hyprscroller PRIVATE spdlog::spdlog_header_only)
set_target_properties(hyprscroller PROPERTIES PREFIX "")
