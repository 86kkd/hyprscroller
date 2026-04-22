#include "overview/session_selection.h"

#include "test_suite.h"
#include "test_support.h"

namespace {

void test_initial_selection_uses_empty_region_when_no_targets_exist() {
    const auto choice = Overview::chooseInitialSelectionChoice(false, false, false, false, true);
    expect_eq(choice, Overview::InitialSelectionChoice::InitialEmpty, "empty region is used when overview has no targets");
}

void test_initial_selection_prefers_origin_window() {
    const auto choice = Overview::chooseInitialSelectionChoice(true, true, true, true, true);
    expect_eq(choice, Overview::InitialSelectionChoice::OriginWindow, "origin window has highest selection priority");
}

void test_initial_selection_falls_back_to_origin_workspace() {
    const auto choice = Overview::chooseInitialSelectionChoice(true, false, true, true, true);
    expect_eq(choice, Overview::InitialSelectionChoice::OriginWorkspace, "origin workspace is used before first target");
}

void test_initial_selection_falls_back_to_first_target() {
    const auto choice = Overview::chooseInitialSelectionChoice(true, false, false, true, true);
    expect_eq(choice, Overview::InitialSelectionChoice::FirstTarget, "first target is used when origin is unavailable");
}

void test_initial_selection_reports_none_without_candidates() {
    const auto choice = Overview::chooseInitialSelectionChoice(false, false, false, false, false);
    expect_eq(choice, Overview::InitialSelectionChoice::None, "no candidates yields no initial selection");
}

void test_overview_dismiss_ignores_modifier_release() {
    expect_true(!Overview::shouldDismissOnKeyRelease(true, true, true, false),
                "modifier-only key releases do not dismiss overview");
}

void test_overview_dismiss_requires_unhandled_release() {
    expect_true(Overview::shouldDismissOnKeyRelease(true, true, false, false),
                "unhandled key release dismisses overview");
    expect_true(!Overview::shouldDismissOnKeyRelease(true, true, false, true),
                "handled key release keeps overview active");
    expect_true(!Overview::shouldDismissOnKeyRelease(true, false, false, false),
                "key press does not dismiss overview");
}

} // namespace

void run_overview_session_logic_tests() {
    test_initial_selection_uses_empty_region_when_no_targets_exist();
    test_initial_selection_prefers_origin_window();
    test_initial_selection_falls_back_to_origin_workspace();
    test_initial_selection_falls_back_to_first_target();
    test_initial_selection_reports_none_without_candidates();
    test_overview_dismiss_ignores_modifier_release();
    test_overview_dismiss_requires_unhandled_release();
}
