#include "overview/navigation/selection.h"

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

void test_overview_close_waits_for_final_modifier_release() {
    expect_true(!Overview::shouldCloseOverviewOnKeyRelease(true, true, true, false, false),
                "modifier release keeps overview open while modifiers remain pressed");
}

void test_overview_close_requires_unhandled_release() {
    expect_true(Overview::shouldCloseOverviewOnKeyRelease(true, true, false, false, false),
                "unhandled key release closes overview");
    expect_true(!Overview::shouldCloseOverviewOnKeyRelease(true, true, false, true, false),
                "handled key release keeps overview active");
    expect_true(!Overview::shouldCloseOverviewOnKeyRelease(true, false, false, false, false),
                "key press does not close overview");
}

void test_overview_close_accepts_final_modifier_release() {
    expect_true(Overview::shouldCloseOverviewOnKeyRelease(true, true, true, false, true),
                "modifier release closes overview once no transient modifiers remain");
    expect_true(!Overview::shouldCloseOverviewOnKeyRelease(false, true, true, false, true),
                "inactive overview ignores modifier release");
}

void test_input_handling_state_survives_until_release() {
    Overview::InputHandlingState state;
    state.markHandled();

    expect_true(state.consume(false),
                "handled press is visible to the first key event");
    expect_true(state.consume(true),
                "handled press also suppresses the matching key release");
    expect_true(!state.consume(true),
                "release suppression is consumed exactly once");
}

} // namespace

void run_overview_session_logic_tests() {
    test_initial_selection_uses_empty_region_when_no_targets_exist();
    test_initial_selection_prefers_origin_window();
    test_initial_selection_falls_back_to_origin_workspace();
    test_initial_selection_falls_back_to_first_target();
    test_initial_selection_reports_none_without_candidates();
    test_overview_close_waits_for_final_modifier_release();
    test_overview_close_requires_unhandled_release();
    test_overview_close_accepts_final_modifier_release();
    test_input_handling_state_survives_until_release();
}
