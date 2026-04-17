#pragma once

#include <cmath>
#include <iostream>
#include <string_view>

inline int failures = 0;

inline void expect_true(bool condition, std::string_view message) {
    if (condition)
        return;

    std::cerr << "FAIL: " << message << '\n';
    ++failures;
}

template <typename T>
inline void expect_eq(const T &actual, const T &expected, std::string_view message) {
    if (actual == expected)
        return;

    std::cerr << "FAIL: " << message << '\n';
    ++failures;
}

inline void expect_near(double actual, double expected, double epsilon, std::string_view message) {
    if (std::abs(actual - expected) <= epsilon)
        return;

    std::cerr << "FAIL: " << message << " actual=" << actual << " expected=" << expected << '\n';
    ++failures;
}
