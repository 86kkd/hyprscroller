#include <string>

#include "core/canvas_workspace_snapshot.h"
#include "core/layout_snapshot.h"

#include "test_suite.h"
#include "test_support.h"

namespace {

void expect_box_eq(const ScrollerCore::Box &actual, const ScrollerCore::Box &expected, std::string_view label) {
    expect_near(actual.x, expected.x, 1e-9, std::string(label) + " x");
    expect_near(actual.y, expected.y, 1e-9, std::string(label) + " y");
    expect_near(actual.w, expected.w, 1e-9, std::string(label) + " w");
    expect_near(actual.h, expected.h, 1e-9, std::string(label) + " h");
}

void test_snapshot_round_trip() {
    using namespace ScrollerSnapshot;

    RepositorySnapshot repository;
    CanvasSnapshot canvas;
    canvas.workspaceId = 7;
    canvas.activeLaneIndex = 1;

    LaneSnapshot firstLane;
    firstLane.mode = static_cast<int>(Mode::Row);
    firstLane.reorder = 1;
    firstLane.ephemeral = false;
    firstLane.activeStackIndex = 0;

    StackSnapshot firstStack;
    firstStack.width = 3;
    firstStack.reorder = 1;
    firstStack.fullscreened = true;
    firstStack.maximized = false;
    firstStack.geom = {10.0, 20.0, 300.0, 400.0};
    firstStack.memGeom = {1.0, 2.0, 3.0, 4.0};
    firstStack.activeWindowKey = 0x22;
    firstStack.windows.push_back({.key = 0x11, .heightMode = 0, .geomY = 50.0, .geomH = 150.0, .memY = 5.0, .memH = 15.0});
    firstStack.windows.push_back({.key = 0x22, .heightMode = 5, .geomY = 200.0, .geomH = 125.0, .memY = 25.0, .memH = 35.0});
    firstLane.stacks.push_back(firstStack);

    LaneSnapshot secondLane;
    secondLane.mode = static_cast<int>(Mode::Column);
    secondLane.reorder = 0;
    secondLane.ephemeral = true;
    secondLane.activeStackIndex = 0;
    secondLane.stacks.push_back({
        .width = 1,
        .reorder = 0,
        .fullscreened = false,
        .maximized = true,
        .geom = {100.0, 120.0, 800.0, 900.0},
        .memGeom = {101.0, 121.0, 700.0, 800.0},
        .activeWindowKey = 0x33,
        .windows = {{.key = 0x33, .heightMode = 4, .geomY = 15.0, .geomH = 600.0, .memY = 10.0, .memH = 580.0}},
    });

    canvas.lanes.push_back(firstLane);
    canvas.lanes.push_back(secondLane);
    repository.emplace(canvas.workspaceId, canvas);

    const auto serialized = serialize_repository(repository);
    const auto parsed = deserialize_repository(serialized);
    expect_true(parsed.has_value(), "layout snapshot round trip parses");
    if (!parsed)
        return;

    const auto it = parsed->find(7);
    expect_true(it != parsed->end(), "layout snapshot round trip keeps workspace");
    if (it == parsed->end())
        return;

    const auto &restored = it->second;
    expect_eq(restored.workspaceId, 7, "layout snapshot keeps workspace id");
    expect_eq(restored.activeLaneIndex, static_cast<size_t>(1), "layout snapshot keeps active lane index");
    expect_eq(restored.lanes.size(), static_cast<size_t>(2), "layout snapshot keeps lane count");
    expect_eq(restored.lanes[0].stacks.size(), static_cast<size_t>(1), "layout snapshot keeps stack count");
    expect_eq(restored.lanes[0].stacks[0].windows.size(), static_cast<size_t>(2), "layout snapshot keeps window count");
    expect_true(restored.lanes[1].ephemeral, "layout snapshot keeps lane ephemeral flag");
    expect_true(restored.lanes[0].stacks[0].fullscreened, "layout snapshot keeps fullscreened flag");
    expect_true(restored.lanes[1].stacks[0].maximized, "layout snapshot keeps maximized flag");
    expect_eq(restored.lanes[0].stacks[0].activeWindowKey, static_cast<uintptr_t>(0x22), "layout snapshot keeps active window key");
    expect_eq(restored.lanes[0].stacks[0].windows[1].heightMode, 5, "layout snapshot keeps window height mode");
    expect_box_eq(restored.lanes[1].stacks[0].geom, {100.0, 120.0, 800.0, 900.0}, "layout snapshot keeps stack geom");
}

void test_snapshot_rejects_invalid_version() {
    const auto parsed = ScrollerSnapshot::deserialize_repository("VERSION 99\n");
    expect_true(!parsed.has_value(), "layout snapshot rejects unsupported versions");
}

void test_snapshot_rejects_malformed_stack_line() {
    const std::string malformed =
        "VERSION 1\n"
        "WORKSPACE 4 0 1\n"
        "LANE 0 0 0 1\n"
        "STACK 1 0 0 0 1 2 3\n";
    const auto parsed = ScrollerSnapshot::deserialize_repository(malformed);
    expect_true(!parsed.has_value(), "layout snapshot rejects malformed stack lines");
}

void test_canvas_workspace_snapshot_round_trip() {
    using namespace ScrollerCanvasSnapshot;

    RepositorySnapshot repository;
    repository.activeCanvasId = 4;
    repository.canvases.push_back({
        .canvasId = 4,
        .tileX = 1,
        .tileY = -1,
        .members = {
            {.monitorId = 1, .workspaceId = 7, .special = false},
            {.monitorId = 2, .workspaceId = 12, .special = true},
        },
    });

    const auto serialized = serialize_repository(repository);
    const auto parsed = deserialize_repository(serialized);
    expect_true(parsed.has_value(), "canvas workspace snapshot round trip parses");
    if (!parsed)
        return;

    expect_eq(parsed->activeCanvasId, 4, "canvas workspace snapshot keeps active canvas id");
    expect_eq(parsed->canvases.size(), static_cast<size_t>(1), "canvas workspace snapshot keeps canvas count");
    expect_eq(parsed->canvases[0].canvasId, 4, "canvas workspace snapshot keeps canvas id");
    expect_eq(parsed->canvases[0].tileX, 1, "canvas workspace snapshot keeps tile x");
    expect_eq(parsed->canvases[0].tileY, -1, "canvas workspace snapshot keeps tile y");
    expect_eq(parsed->canvases[0].members.size(), static_cast<size_t>(2), "canvas workspace snapshot keeps member count");
    expect_true(parsed->canvases[0].members[1].special, "canvas workspace snapshot keeps special member flag");
}

void test_canvas_workspace_snapshot_rejects_missing_active_line() {
    const auto parsed = ScrollerCanvasSnapshot::deserialize_repository(
        "VERSION 1\n"
        "CANVAS 0 0 0 0\n");
    expect_true(!parsed.has_value(), "canvas workspace snapshot rejects missing ACTIVE line");
}

} // namespace

void run_layout_snapshot_tests() {
    test_snapshot_round_trip();
    test_snapshot_rejects_invalid_version();
    test_snapshot_rejects_malformed_stack_line();
    test_canvas_workspace_snapshot_round_trip();
    test_canvas_workspace_snapshot_rejects_missing_active_line();
}
