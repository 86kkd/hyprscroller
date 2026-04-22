#include <vector>

#include "overview/scene_layout.h"

#include "test_suite.h"
#include "test_support.h"

namespace {

void test_localize_global_box_keeps_monitor_local_axes() {
    const auto localized = Overview::localizeGlobalBox({410.0, 260.0, 120.0, 90.0}, 400.0, 200.0);

    expect_near(localized.x, 10.0, 1e-9, "scene localization subtracts the monitor x origin");
    expect_near(localized.y, 60.0, 1e-9, "scene localization subtracts the monitor y origin");
    expect_near(localized.w, 120.0, 1e-9, "scene localization preserves width");
    expect_near(localized.h, 90.0, 1e-9, "scene localization preserves height");
}

void test_project_boxes_to_content_preserves_order_and_fits_bounds() {
    const std::vector<ScrollerCore::Box> source = {
        {0.0, 0.0, 100.0, 80.0},
        {120.0, 0.0, 90.0, 80.0},
    };
    const auto projected = Overview::projectBoxesToContent(source, {10.0, 20.0, 300.0, 180.0});

    expect_eq(projected.size(), static_cast<size_t>(2), "projection returns one box per source box");
    expect_true(projected[0].x < projected[1].x, "projection preserves left-to-right ordering");
    expect_true(projected[0].x >= 10.0 && projected[0].y >= 20.0, "projection stays inside content origin");
    expect_true(projected[1].x + projected[1].w <= 310.0, "projection stays inside content max x");
    expect_true(projected[0].y + projected[0].h <= 200.0, "projection stays inside content max y");
}

void test_project_global_boxes_to_content_tracks_workspace_card_position() {
    const std::vector<ScrollerCore::Box> source = {
        {0.0, 0.0, 100.0, 80.0},
        {120.0, 0.0, 90.0, 80.0},
    };

    const auto leftContent = Overview::buildWorkspaceContentBox({0.0, 0.0, 220.0, 180.0});
    const auto rightContent = Overview::buildWorkspaceContentBox({260.0, 0.0, 220.0, 180.0});
    const auto leftProjected = Overview::projectGlobalBoxesToContent(source, leftContent, 0.0, 0.0);
    const auto rightProjected = Overview::projectGlobalBoxesToContent(source, rightContent, 0.0, 0.0);

    expect_true(leftProjected[0].x < rightProjected[0].x,
                "global projection respects the workspace card's global x position");
    expect_true(rightProjected[1].x + rightProjected[1].w <= rightContent.x + rightContent.w,
                "global projection stays inside the destination content box");
}

void test_empty_workspace_preview_box_is_inset() {
    const auto preview = Overview::buildEmptyWorkspacePreviewBox({0.0, 0.0, 240.0, 160.0});
    expect_true(preview.x > 0.0 && preview.y > 0.0, "empty workspace preview is inset from content box");
    expect_true(preview.w < 240.0 && preview.h < 160.0, "empty workspace preview shrinks relative to content box");
}

void test_workspace_content_box_reserves_header_band() {
    const auto content = Overview::buildWorkspaceContentBox({10.0, 20.0, 240.0, 160.0});
    expect_true(content.x > 10.0 && content.y > 20.0, "workspace content box insets from the outer workspace frame");
    expect_true(content.h < 160.0, "workspace content box reserves header height");
}

} // namespace

void run_overview_scene_tests() {
    test_localize_global_box_keeps_monitor_local_axes();
    test_project_boxes_to_content_preserves_order_and_fits_bounds();
    test_project_global_boxes_to_content_tracks_workspace_card_position();
    test_empty_workspace_preview_box_is_inset();
    test_workspace_content_box_reserves_header_band();
}
