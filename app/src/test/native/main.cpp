#include <cstdio>

#include "test_harness.h"

/// Runs every registered test and returns a non-zero exit code if any check failed.
int main() {
    int failedTests = 0;
    for (const auto& test : testing::registry()) {
        const int before = testing::failureCount();
        std::printf("[ RUN  ] %s\n", test.name);
        test.body();
        if (testing::failureCount() == before) {
            std::printf("[  OK  ] %s\n", test.name);
        } else {
            std::printf("[ FAIL ] %s\n", test.name);
            ++failedTests;
        }
    }
    std::printf("\n%zu tests, %d failed\n", testing::registry().size(), failedTests);
    return failedTests == 0 ? 0 : 1;
}
