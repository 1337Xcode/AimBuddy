#ifndef AIMBUDDY_TEST_HARNESS_H
#define AIMBUDDY_TEST_HARNESS_H

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <vector>

/**
 * Minimal test runner for the pure C++ core, so the tests build with any C++17 compiler and
 * need no dependency. Register a test with TEST(name) { ... } and check with CHECK / CHECK_NEAR.
 */
namespace testing {

struct TestCase {
    const char* name;
    std::function<void()> body;
};

inline std::vector<TestCase>& registry() {
    static std::vector<TestCase> tests;
    return tests;
}

inline int& failureCount() {
    static int failures = 0;
    return failures;
}

struct Registrar {
    Registrar(const char* name, std::function<void()> body) { registry().push_back({name, std::move(body)}); }
};

/// Small deterministic random source, so a failing run can be reproduced exactly.
class Random {
public:
    explicit Random(uint32_t seed) : state_(seed) {}

    /// Uniform value in [0, 1).
    double uniform() {
        state_ = state_ * 1664525u + 1013904223u;
        return static_cast<double>(state_ >> 8) / 16777216.0;
    }

    /// Normally distributed value with the given standard deviation (Box-Muller).
    double gaussian(double sigma) {
        const double u1 = std::max(uniform(), 1e-12);
        const double u2 = uniform();
        return sigma * std::sqrt(-2.0 * std::log(u1)) * std::cos(6.283185307179586 * u2);
    }

private:
    uint32_t state_;
};

}  // namespace testing

#define TEST(name)                                                              \
    static void test_##name();                                                  \
    static testing::Registrar registrar_##name(#name, test_##name);             \
    static void test_##name()

#define CHECK(condition)                                                        \
    do {                                                                        \
        if (!(condition)) {                                                     \
            std::printf("    FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
            ++testing::failureCount();                                          \
        }                                                                       \
    } while (0)

#define CHECK_NEAR(actual, expected, tolerance)                                                       \
    do {                                                                                              \
        const double a_ = static_cast<double>(actual);                                                \
        const double e_ = static_cast<double>(expected);                                              \
        if (!(std::fabs(a_ - e_) <= static_cast<double>(tolerance))) {                                \
            std::printf("    FAIL %s:%d: %s = %.4f, expected %.4f +/- %.4f\n", __FILE__, __LINE__,    \
                        #actual, a_, e_, static_cast<double>(tolerance));                             \
            ++testing::failureCount();                                                                \
        }                                                                                             \
    } while (0)

#endif  // AIMBUDDY_TEST_HARNESS_H
