#pragma once
#include <array>
#include <vector>

struct F2fLabelNode {
    struct Child {
        int index;
        std::array<std::array<double, 2>, 2> cost;
    };
    std::vector<Child> children;
    std::array<int, 2> localHbt;
    std::array<double, 2> localCost;
};

struct F2fDieAssignment {
    std::vector<int> labels;
    int hbtCount;
    double estimatedCost;
};

// One possible crossing per tree edge plus the maximum local mismatch
// contribution at each projected node.
int f2fDieAssignmentHbtUpperBound(const std::vector<F2fLabelNode>& tree);

// Search every structurally possible HBT-count state. There is deliberately
// no user-specified max_hbt argument.
std::vector<F2fDieAssignment> searchF2fDieAssignments(
    const std::vector<F2fLabelNode>& tree);
