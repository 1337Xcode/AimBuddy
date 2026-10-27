#include "core/assignment.h"
#include "test_harness.h"

using aimbuddy::solveAssignment;

TEST(assignment_picks_cheapest_total_pairing) {
    // Greedy "each row takes its best column" would give row 0 -> col 0 (cost 1) and then
    // be forced to pay 10 for row 1. The cheapest total is row 0 -> col 1, row 1 -> col 0.
    const float cost[] = {1.0f, 2.0f,
                          3.0f, 10.0f};
    int columnForRow[2];
    solveAssignment(cost, 2, 2, 100.0f, columnForRow);
    CHECK(columnForRow[0] == 1);
    CHECK(columnForRow[1] == 0);
}

TEST(assignment_leaves_blocked_pairs_unmatched) {
    const float cost[] = {0.2f, 100.0f,
                          100.0f, 100.0f};
    int columnForRow[2];
    solveAssignment(cost, 2, 2, 100.0f, columnForRow);
    CHECK(columnForRow[0] == 0);
    CHECK(columnForRow[1] == -1);
}

TEST(assignment_handles_more_rows_than_columns) {
    const float cost[] = {0.5f,
                          0.1f,
                          0.9f};
    int columnForRow[3];
    solveAssignment(cost, 3, 1, 100.0f, columnForRow);
    CHECK(columnForRow[0] == -1);
    CHECK(columnForRow[1] == 0);
    CHECK(columnForRow[2] == -1);
}

TEST(assignment_handles_more_columns_than_rows) {
    const float cost[] = {0.9f, 0.1f, 0.5f};
    int columnForRow[1];
    solveAssignment(cost, 1, 3, 100.0f, columnForRow);
    CHECK(columnForRow[0] == 1);
}

TEST(assignment_accepts_empty_input) {
    int columnForRow[1] = {7};
    solveAssignment(nullptr, 0, 0, 100.0f, columnForRow);
    CHECK(columnForRow[0] == 7);  // nothing to write
}

TEST(assignment_matches_brute_force_on_random_matrices) {
    testing::Random random(42);
    for (int round = 0; round < 200; ++round) {
        const int n = 2 + static_cast<int>(random.uniform() * 4.0);  // 2..5
        float cost[25];
        for (int i = 0; i < n * n; ++i) {
            cost[i] = static_cast<float>(random.uniform());
        }
        int columnForRow[5];
        solveAssignment(cost, n, n, 100.0f, columnForRow);
        float solved = 0.0f;
        for (int r = 0; r < n; ++r) {
            solved += cost[r * n + columnForRow[r]];
        }

        // Try every permutation and keep the cheapest.
        int perm[5] = {0, 1, 2, 3, 4};
        float best = 1e9f;
        auto next = [&](auto&& self, int k) -> void {
            if (k == n) {
                float total = 0.0f;
                for (int r = 0; r < n; ++r) {
                    total += cost[r * n + perm[r]];
                }
                best = std::min(best, total);
                return;
            }
            for (int i = k; i < n; ++i) {
                std::swap(perm[k], perm[i]);
                self(self, k + 1);
                std::swap(perm[k], perm[i]);
            }
        };
        next(next, 0);
        CHECK_NEAR(solved, best, 1e-4);
    }
}
