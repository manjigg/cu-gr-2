#include "../../src/gr/F2fCandidateSearch.h"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <map>

int main() {
    // 枚举五节点树的全部 Die 标签，对照每个 k 的最优代价和回溯数量。
    std::vector<F2fLabelNode> tree(5);
    tree[0].children = {{1, {{{1, 6}, {5, 2}}}}, {2, {{{1, 5}, {6, 4}}}}};
    tree[1].children = {{3, {{{2, 5}, {4, 1}}}}, {4, {{{1, 5}, {5, 1}}}}};
    tree[0].localHbt = {{0, 1}};
    tree[0].localCost = {{0, 6}};
    tree[2].localHbt = {{1, 0}};
    tree[2].localCost = {{4, 0}};
    tree[3].localHbt = {{1, 1}};
    tree[3].localCost = {{3, 4}};
    tree[4].localHbt = {{0, 1}};
    tree[4].localCost = {{0, 5}};
    auto measure = [&](const std::vector<int>& labels) {
        double cost = 0;
        int count = 0;
        for (int i = 0; i < 5; ++i) {
            const auto d = labels[i];
            cost += tree[i].localCost[d];
            count += tree[i].localHbt[d];
            for (const auto& child : tree[i].children) {
                cost += child.cost[d][labels[child.index]];
                count += d != labels[child.index];
            }
        }
        return std::make_pair(count, cost);
    };
    std::map<int, std::vector<double>> exhaustive;
    for (int mask = 0; mask < 32; ++mask) {
        std::vector<int> labels(5);
        for (int i = 0; i < 5; ++i) labels[i] = (mask >> i) & 1;
        const auto measured = measure(labels);
        if (measured.first >= 1 && measured.first <= 4) exhaustive[measured.first].push_back(measured.second);
    }
    for (auto& item : exhaustive) std::sort(item.second.begin(), item.second.end());
    std::map<int, int> counts;
    for (const auto& result : searchF2fDieAssignments(tree, 4)) {
        const auto measured = measure(result.labels);
        assert(measured.first == result.hbtCount);
        assert(std::abs(measured.second - result.estimatedCost) < 1e-10);
        assert(measured.second == exhaustive.at(measured.first).at(counts[measured.first]++));
    }
    for (const auto& item : exhaustive) assert(counts[item.first] == std::min<int>(2, item.second.size()));
}
