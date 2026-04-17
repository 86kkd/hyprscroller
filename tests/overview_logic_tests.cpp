#include <vector>

#include "core/layout_math.h"
#include "overview/logic.h"
#include "overview/orientation_math.h"

#include "test_suite.h"
#include "test_support.h"

namespace {

void test_overview_projection() {
    const ScrollerCore::Box visible(0.0, 0.0, 200.0, 100.0);
    const std::vector<ScrollerCore::OverviewRect> items = {
        {.x0 = 10.0, .x1 = 60.0, .y0 = 20.0, .y1 = 70.0},
        {.x0 = 60.0, .x1 = 110.0, .y0 = 10.0, .y1 = 90.0},
    };

    const auto projection = ScrollerCore::compute_overview_projection(items, visible);
    expect_near(projection.min.x, 10.0, 1e-9, "overview projection tracks minimum x");
    expect_near(projection.min.y, 10.0, 1e-9, "overview projection tracks minimum y");
    expect_near(projection.max.x, 110.0, 1e-9, "overview projection tracks maximum x");
    expect_near(projection.max.y, 90.0, 1e-9, "overview projection tracks maximum y");
    expect_near(projection.width, 100.0, 1e-9, "overview projection width is derived from bounds");
    expect_near(projection.height, 80.0, 1e-9, "overview projection height is derived from bounds");
    expect_near(projection.scale, 1.25, 1e-9, "overview projection chooses the limiting scale");
    expect_near(projection.offset.x, 37.5, 1e-9, "overview projection centers on x");
    expect_near(projection.offset.y, 0.0, 1e-9, "overview projection centers on y");

    const std::vector<ScrollerCore::OverviewRect> degenerate = {
        {.x0 = 50.0, .x1 = 50.0, .y0 = 10.0, .y1 = 40.0},
    };
    const auto degenerateProjection = ScrollerCore::compute_overview_projection(degenerate, visible);
    expect_near(degenerateProjection.scale, 1.0, 1e-9, "degenerate projection keeps scale at 1");
    expect_near(degenerateProjection.offset.x, 50.0, 1e-9, "degenerate projection preserves raw x offset");
    expect_near(degenerateProjection.offset.y, 10.0, 1e-9, "degenerate projection preserves raw y offset");
}

void test_monitor_space_orientation() {
    using Overview::MonitorOrientation;

    expect_eq(Overview::orientation_for_transform(WL_OUTPUT_TRANSFORM_NORMAL),
              MonitorOrientation::Landscape,
              "normal transform is landscape");
    expect_eq(Overview::orientation_for_transform(WL_OUTPUT_TRANSFORM_180),
              MonitorOrientation::Landscape,
              "180 transform stays landscape");
    expect_eq(Overview::orientation_for_transform(WL_OUTPUT_TRANSFORM_90),
              MonitorOrientation::Portrait,
              "90 transform is portrait");
    expect_eq(Overview::orientation_for_transform(WL_OUTPUT_TRANSFORM_270),
              MonitorOrientation::Portrait,
              "270 transform is portrait");

    const auto portraitRenderBox = Overview::transform_box_to_render_space({10.0, 20.0, 100.0, 200.0},
                                                                           WL_OUTPUT_TRANSFORM_270,
                                                                           1080.0,
                                                                           1920.0);
    expect_near(portraitRenderBox.x, 860.0, 1e-9, "portrait transform remaps x into render space");
    expect_near(portraitRenderBox.y, 10.0, 1e-9, "portrait transform remaps y into render space");
    expect_near(portraitRenderBox.w, 200.0, 1e-9, "portrait transform swaps width");
    expect_near(portraitRenderBox.h, 100.0, 1e-9, "portrait transform swaps height");

    const auto landscapeRenderBox = Overview::transform_box_to_render_space({10.0, 20.0, 100.0, 200.0},
                                                                            WL_OUTPUT_TRANSFORM_NORMAL,
                                                                            1920.0,
                                                                            1080.0);
    expect_near(landscapeRenderBox.x, 10.0, 1e-9, "landscape transform keeps x stable");
    expect_near(landscapeRenderBox.y, 20.0, 1e-9, "landscape transform keeps y stable");
    expect_near(landscapeRenderBox.w, 100.0, 1e-9, "landscape transform keeps width stable");
    expect_near(landscapeRenderBox.h, 200.0, 1e-9, "landscape transform keeps height stable");
}

void test_overview_target_selection_across_monitors() {
    const std::vector<OverviewLogic::TargetCandidate> targets = {
        {.monitorId = 1, .box = {0.0, 0.0, 100.0, 100.0}},
        {.monitorId = 1, .box = {120.0, 0.0, 100.0, 100.0}},
        {.monitorId = 2, .box = {400.0, 0.0, 100.0, 100.0}},
    };

    const auto next = OverviewLogic::pickTargetIndex(targets, 1, Direction::Right);
    expect_true(next.has_value() && *next == 2,
                "overview target selection crosses to the next monitor when the nearest target is there");
}

void test_overview_empty_target_region_selection() {
    const std::vector<OverviewLogic::RegionCandidate> regions = {
        {.monitorId = 1, .box = {0.0, 0.0, 300.0, 300.0}},
        {.monitorId = 2, .box = {320.0, 0.0, 300.0, 300.0}},
    };

    const ScrollerCore::Box sourceBox(260.0, 120.0, 80.0, 80.0);
    const auto regionIndex = OverviewLogic::pickRegionIndexForSyntheticTarget(regions, 0, sourceBox, Direction::Right);
    expect_true(regionIndex.has_value() && *regionIndex == 1,
                "overview empty target chooses the adjacent monitor region when crossing monitor bounds");
}

void test_overview_empty_accept_plan() {
    const auto plan = OverviewLogic::buildEmptyAcceptPlan(7, 42);
    expect_eq(plan.size(), static_cast<size_t>(2), "overview empty accept plan emits two steps");
    expect_eq(plan[0].type, OverviewLogic::AcceptActionType::FocusMonitor,
              "overview empty accept plan focuses the monitor first");
    expect_eq(plan[0].monitorId, 7, "overview empty accept plan keeps the requested monitor id");
    expect_eq(plan[1].type, OverviewLogic::AcceptActionType::Workspace,
              "overview empty accept plan switches workspace second");
    expect_eq(plan[1].workspaceId, static_cast<OverviewLogic::WorkspaceId>(42),
              "overview empty accept plan keeps the requested workspace id");
}

void test_overview_window_accept_plan() {
    const auto plan = OverviewLogic::buildWorkspaceAcceptPlan(5, 17, false);
    expect_eq(plan.size(), static_cast<size_t>(2), "overview window accept plan emits two steps");
    expect_eq(plan[0].type, OverviewLogic::AcceptActionType::FocusMonitor,
              "overview window accept plan focuses the monitor first");
    expect_eq(plan[0].monitorId, 5, "overview window accept plan keeps the requested monitor id");
    expect_eq(plan[1].type, OverviewLogic::AcceptActionType::Workspace,
              "overview window accept plan switches normal workspace second");
    expect_eq(plan[1].workspaceId, static_cast<OverviewLogic::WorkspaceId>(17),
              "overview window accept plan keeps the requested workspace id");
}

void test_overview_special_workspace_accept_plan() {
    const auto plan = OverviewLogic::buildWorkspaceAcceptPlan(3, 88, true);
    expect_eq(plan.size(), static_cast<size_t>(2), "overview special accept plan emits two steps");
    expect_eq(plan[0].type, OverviewLogic::AcceptActionType::FocusMonitor,
              "overview special accept plan focuses the monitor first");
    expect_eq(plan[1].type, OverviewLogic::AcceptActionType::ToggleSpecialWorkspace,
              "overview special accept plan toggles special workspace second");
    expect_eq(plan[1].workspaceId, static_cast<OverviewLogic::WorkspaceId>(88),
              "overview special accept plan keeps the requested workspace id");
}

} // namespace

void run_overview_logic_tests() {
    test_overview_projection();
    test_monitor_space_orientation();
    test_overview_target_selection_across_monitors();
    test_overview_empty_target_region_selection();
    test_overview_empty_accept_plan();
    test_overview_window_accept_plan();
    test_overview_special_workspace_accept_plan();
}
