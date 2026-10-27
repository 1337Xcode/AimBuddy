#ifndef AIMBUDDY_CORE_ASSIGNMENT_H
#define AIMBUDDY_CORE_ASSIGNMENT_H

#include <algorithm>

namespace aimbuddy {

/// Largest number of rows or columns solveAssignment() accepts.
constexpr int kMaxAssignmentSize = 64;

/**
 * Pairs rows with columns so the total cost is as low as possible (Hungarian algorithm).
 *
 * `cost` is a rows x cols matrix in row-major order. Any pair whose cost is `blockedCost` or
 * more is treated as not allowed and is never reported. `columnForRow[r]` receives the column
 * paired with row r, or -1 when row r has no allowed partner. Each column is used at most once.
 *
 * Choosing the globally cheapest pairing, instead of letting each row grab its best column in
 * turn, stops two nearby targets from swapping identities when their boxes overlap.
 */
inline void solveAssignment(const float* cost, int rows, int cols, float blockedCost,
                            int* columnForRow) {
    for (int r = 0; r < rows; ++r) {
        columnForRow[r] = -1;
    }
    rows = std::min(rows, kMaxAssignmentSize);
    cols = std::min(cols, kMaxAssignmentSize);
    const int size = std::max(rows, cols);
    if (size == 0) {
        return;
    }

    // Pad to a square matrix; padded cells are "blocked" so they only absorb the surplus rows or columns.
    auto cell = [&](int row, int col) -> double {
        return (row < rows && col < cols) ? static_cast<double>(cost[row * cols + col])
                                          : static_cast<double>(blockedCost);
    };

    // Shortest augmenting path with row and column potentials, 1-based as in the classic formulation.
    double rowPotential[kMaxAssignmentSize + 1] = {};
    double colPotential[kMaxAssignmentSize + 1] = {};
    int rowOfCol[kMaxAssignmentSize + 1] = {};
    int previousCol[kMaxAssignmentSize + 1] = {};
    double minReduced[kMaxAssignmentSize + 1];
    bool colVisited[kMaxAssignmentSize + 1];
    constexpr double kInfinity = 1e30;

    for (int row = 1; row <= size; ++row) {
        rowOfCol[0] = row;
        int col0 = 0;
        std::fill(minReduced, minReduced + size + 1, kInfinity);
        std::fill(colVisited, colVisited + size + 1, false);
        do {
            colVisited[col0] = true;
            const int row0 = rowOfCol[col0];
            double delta = kInfinity;
            int col1 = 0;
            for (int col = 1; col <= size; ++col) {
                if (colVisited[col]) {
                    continue;
                }
                const double reduced = cell(row0 - 1, col - 1) - rowPotential[row0] - colPotential[col];
                if (reduced < minReduced[col]) {
                    minReduced[col] = reduced;
                    previousCol[col] = col0;
                }
                if (minReduced[col] < delta) {
                    delta = minReduced[col];
                    col1 = col;
                }
            }
            for (int col = 0; col <= size; ++col) {
                if (colVisited[col]) {
                    rowPotential[rowOfCol[col]] += delta;
                    colPotential[col] -= delta;
                } else {
                    minReduced[col] -= delta;
                }
            }
            col0 = col1;
        } while (rowOfCol[col0] != 0);
        do {
            const int col1 = previousCol[col0];
            rowOfCol[col0] = rowOfCol[col1];
            col0 = col1;
        } while (col0 != 0);
    }

    for (int col = 1; col <= size; ++col) {
        const int row = rowOfCol[col] - 1;
        if (row >= 0 && row < rows && col - 1 < cols && cell(row, col - 1) < blockedCost) {
            columnForRow[row] = col - 1;
        }
    }
}

}  // namespace aimbuddy

#endif  // AIMBUDDY_CORE_ASSIGNMENT_H
