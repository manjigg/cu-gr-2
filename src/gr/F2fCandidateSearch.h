#pragma once
#include <array>
#include <vector>

// 投影树上的估价使用与局部 Wire/Via/拥塞相同的成本尺度。
struct F2fLabelNode {
    struct Child {
        int index;
        std::array<std::array<double, 2>, 2> cost;  // 父 Die → 子 Die
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

// 节点顺序是父节点在前的树遍历序。每个 HBT 数量保留至多两条回溯链。
std::vector<F2fDieAssignment> searchF2fDieAssignments(const std::vector<F2fLabelNode>& tree,
                                                       int maxHbt);
