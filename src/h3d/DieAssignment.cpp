#include "DieAssignment.h"

#include <array>
#include <algorithm>
#include <limits>
#include <stdexcept>

namespace h3d {
namespace {

constexpr int numDies = 2;

struct State {
    int cost = std::numeric_limits<int>::max();
    int low = 0;
    int high = 0;
};

// 返回区间中最便宜的孩子接入 die；同分按显式偏好，DP 与回溯共用此规则。
int bestDie(const std::array<State, numDies>& states, int low, int high, int preferred = 0) {
    int best = low;
    for (int die = low + 1; die <= high; die++) {
        if (states[die].cost < states[best].cost || (states[die].cost == states[best].cost && die == preferred)) best = die;
    }
    return best;
}

} // namespace

DieAssignmentResult DieAssignment::run(const H3DTree& topology, int root, Die tiePreference, std::uint32_t tieSeed) const {
    if (tiePreference != Die::Bottom && tiePreference != Die::Top) throw std::invalid_argument("Invalid die tie preference");
    // 对称重标号后复用同一套公式；Top 偏好恰是 Bottom 平局规则的镜像。
    const int flip = tiePreference == Die::Top ? 1 : 0;
    const RootedTree tree = topology.rootAt(root);
    DieAssignmentResult result;
    result.root = tree.root;
    if (tree.root == -1) return result;

    const auto& nodes = topology.getNodes();
    std::mt19937 random(tieSeed);
    std::vector<int> preferences(nodes.size(), 0);
    if (tieSeed) for (auto& preference : preferences) preference = random() & 1;
    // states[v][d] 对应论文 mhc(v,d)：父边在 die d 时，v 子树所需最少 HBT 数量。
    std::vector<std::array<State, numDies>> states(nodes.size());
    for (auto it = tree.preorder.rbegin(); it != tree.preorder.rend(); ++it) {
        int node = *it;
        int pinLow = numDies;
        int pinHigh = -1;
        for (const auto& pin : nodes[node].pins) {
            int die = static_cast<int>(pin.die) ^ flip;
            pinLow = std::min(pinLow, die);
            pinHigh = std::max(pinHigh, die);
        }

        // 枚举节点处的 die 区间，而非所有孩子的 die 组合。
        // 区间须覆盖固定引脚和父边；每个孩子在区间内独立选最便宜的接入 die。
        // 两 die 情形只有 [0,0]、[0,1]、[1,1]，总时间和内存均为 O(|V|)。
        std::array<std::pair<int, int>, 3> intervals = {{{0, 0}, {0, 1}, {1, 1}}};
        if (tieSeed) std::shuffle(intervals.begin(), intervals.end(), random);
        for (const auto& interval : intervals) {
            int low = interval.first, high = interval.second;
            if (low > pinLow || high < pinHigh) continue;
            int cost = high - low;
            for (int child : tree.children[node]) {
                int childDie = bestDie(states[child], low, high, preferences[child]);
                cost += states[child][childDie].cost;
            }
            for (int parentDie = low; parentDie <= high; parentDie++) {
                if (cost < states[node][parentDie].cost) {
                    states[node][parentDie] = {cost, low, high};
                }
            }
        }
    }

    // 根没有真实父边，自由选择其接入 die，不额外收取任何虚构连接的成本。
    int rootDie = bestDie(states[tree.root], 0, numDies - 1, preferences[tree.root]);
    result.hbtCount = states[tree.root][rootDie].cost;
    result.edgeDies.resize(topology.getEdges().size(), Die::Bottom);
    result.nodeDieSpans.resize(nodes.size(), {Die::Bottom, Die::Bottom});
    result.hbtNodes.reserve(result.hbtCount);
    std::vector<int> incomingDies(nodes.size(), -1);
    incomingDies[tree.root] = rootDie;
    for (int node : tree.preorder) {
        const State& state = states[node][incomingDies[node]];
        result.nodeDieSpans[node] = {static_cast<Die>(flip ? 1 - state.high : state.low),
                                    static_cast<Die>(flip ? 1 - state.low : state.high)};
        if (state.low != state.high) result.hbtNodes.push_back(node);
        for (int child : tree.children[node]) {
            int die = bestDie(states[child], state.low, state.high, preferences[child]);
            incomingDies[child] = die;
            result.edgeDies[tree.parentEdges[child]] = static_cast<Die>(die ^ flip);
        }
    }
    return result;
}

} // namespace h3d
