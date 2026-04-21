#include <vector>

#include "overview/scene_layout.h"

#include "test_suite.h"
#include "test_support.h"

namespace {

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

void test_empty_workspace_preview_box_is_inset() {
    const auto preview = Overview::buildEmptyWorkspacePreviewBox({0.0, 0.0, 240.0, 160.0});
    expect_true(preview.x > 0.0 && preview.y > 0.0, "empty workspace preview is inset from content box");
    expect_true(preview.w < 240.0 && preview.h < 160.0, "empty workspace preview shrinks relative to content box");
}

} // namespace

void run_overview_scene_tests() {
    test_project_boxes_to_content_preserves_order_and_fits_bounds();
    test_empty_workspace_preview_box_is_inset();
}
