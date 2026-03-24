#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string_view>
#include <vector>

#include "core/direction.h"
#include "core/interval.h"
#include "core/layout_math.h"

namespace {

int failures = 0;

void expect_true(bool condition, std::string_view message) {
    if (condition)
        return;

    std::cerr << "FAIL: " << message << '\n';
    ++failures;
}

template <typename T>
void expect_eq(const T &actual, const T &expected, std::string_view message) {
    if (actual == expected)
        return;

    std::cerr << "FAIL: " << message << '\n';
    ++failures;
}

void expect_near(double actual, double expected, double epsilon, std::string_view message) {
    if (std::abs(actual - expected) <= epsilon)
        return;

    std::cerr << "FAIL: " << message << " actual=" << actual << " expected=" << expected << '\n';
    ++failures;
}

void test_interval() {
    expect_true(ScrollerCore::Interval::intersects(0.0, 10.0, 5.0, 15.0), "interval partial overlap intersects");
    expect_true(ScrollerCore::Interval::intersects(0.0, 20.0, 5.0, 15.0), "interval containing viewport intersects");
    expect_true(!ScrollerCore::Interval::intersects(0.0, 5.0, 5.0, 15.0), "touching edge does not intersect");
    expect_true(ScrollerCore::Interval::fully_visible(6.0, 9.0, 5.0, 15.0), "fully visible interval reports true");
    expect_true(!ScrollerCore::Interval::fully_visible(4.0, 9.0, 5.0, 15.0), "partially clipped interval reports false");
}

void test_direction_helpers() {
    expect_eq(std::string_view(ScrollerCore::direction_name(Direction::Begin)), std::string_view("begin"),
              "direction_name returns begin");
    expect_eq(std::string_view(ScrollerCore::direction_dispatch_arg(Direction::Right)), std::string_view("r"),
              "direction_dispatch_arg returns short right");
    expect_true(ScrollerCore::direction_dispatch_arg(Direction::Center) == nullptr,
                "direction_dispatch_arg rejects center");
    expect_eq(ScrollerCore::opposite_direction(Direction::Up), Direction::Down,
              "opposite_direction flips up to down");
}

void test_parse_helpers() {
    const auto down = ScrollerCore::parse_direction_arg("dn");
    expect_true(down.has_value() && *down == Direction::Down, "parse_direction_arg handles dn alias");

    const auto center = ScrollerCore::parse_direction_arg("centre");
    expect_true(center.has_value() && *center == Direction::Center, "parse_direction_arg handles centre alias");

    expect_true(!ScrollerCore::parse_direction_arg("sideways").has_value(),
                "parse_direction_arg rejects invalid input");

    const auto toBeginning = ScrollerCore::parse_fit_size_arg("tobeginning");
    expect_true(toBeginning.has_value() && *toBeginning == FitSize::ToBeg,
                "parse_fit_size_arg handles tobeginning");

    expect_true(!ScrollerCore::parse_fit_size_arg("largest").has_value(),
                "parse_fit_size_arg rejects invalid input");
}

void test_anchor_selection() {
    const ScrollerCore::Box visible(100.0, 50.0, 400.0, 300.0);

    expect_near(ScrollerCore::choose_anchor_x(true, false, 150.0, 120.0, 0.0, 260.0, visible),
                230.0, 1e-9, "choose_anchor_x keeps active and next visible");
    expect_near(ScrollerCore::choose_anchor_x(true, true, 200.0, 250.0, 150.0, 275.0, visible),
                250.0, 1e-9, "choose_anchor_x falls back to prev width when next does not fit");
    expect_near(ScrollerCore::choose_anchor_x(false, true, 350.0, 0.0, 100.0, 275.0, visible),
                150.0, 1e-9, "choose_anchor_x aligns active to right edge when only prev exists");

    expect_near(ScrollerCore::choose_anchor_y(true, false, 120.0, 100.0, 0.0, visible),
                130.0, 1e-9, "choose_anchor_y keeps next visible when possible");
    expect_near(ScrollerCore::choose_anchor_y(false, true, 180.0, 0.0, 100.0, visible),
                150.0, 1e-9, "choose_anchor_y positions after prev when it fits");
    expect_near(ScrollerCore::choose_anchor_y(false, true, 250.0, 0.0, 100.0, visible),
                100.0, 1e-9, "choose_anchor_y aligns to bottom when only prev exists but cannot fit");
}

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

} // namespace

int main() {
    test_interval();
    test_direction_helpers();
    test_parse_helpers();
    test_anchor_selection();
    test_overview_projection();

    if (failures != 0) {
        std::cerr << failures << " logic test(s) failed\n";
        return EXIT_FAILURE;
    }

    std::cout << "All logic tests passed\n";
    return EXIT_SUCCESS;
}
