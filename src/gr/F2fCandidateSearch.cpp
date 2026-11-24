#include "F2fCandidateSearch.h"
#include <algorithm>
#include <cmath>
#include <memory>

namespace {
constexpr int retainedPerState = 2;

struct BranchChoice {
    std::shared_ptr<const BranchChoice> previous;
    int child, die, hbt, rank;
};

struct State {
    double cost;
    std::shared_ptr<const BranchChoice> last;
};

using Frontier = std::vector<std::vector<State>>;
} // namespace

std::vector<F2fDieAssignment> searchF2fDieAssignments(const std::vector<F2fLabelNode>& tree,
                                                       int maxHbt) {
    if (tree.empty() || maxHbt < 1) return {};
    std::vector<std::array<Frontier, 2>> dp(tree.size());
    for (int v = static_cast<int>(tree.size()) - 1; v >= 0; --v) {
        for (int die = 0; die < 2; ++die) {
            Frontier current(maxHbt + 1);
            const int local = tree[v].localHbt[die];
            if (local <= maxHbt && std::isfinite(tree[v].localCost[die])) {
                current[local].push_back({tree[v].localCost[die], nullptr});
            }
            // 树形背包卷积。k 计入子树数量、父子跨 Die 和本节点对侧端点。
            for (const auto& child : tree[v].children) {
                Frontier next(maxHbt + 1);
                for (int used = 0; used <= maxHbt; ++used) {
                    for (const State& left : current[used]) {
                        for (int childDie = 0; childDie < 2; ++childDie) {
                            const int base = used + (childDie != die);
                            for (int k = 0; k + base <= maxHbt; ++k) {
                                const auto& right = dp[child.index][childDie][k];
                                for (int rank = 0; rank < static_cast<int>(right.size()); ++rank) {
                                    const double cost = left.cost + child.cost[die][childDie] + right[rank].cost;
                                    auto& frontier = next[base + k];
                                    if (!std::isfinite(cost) ||
                                        (frontier.size() == retainedPerState && cost >= frontier.back().cost)) continue;
                                    auto pos = std::upper_bound(frontier.begin(), frontier.end(), cost,
                                        [](double value, const State& state) { return value < state.cost; });
                                    frontier.insert(pos, State{cost, std::make_shared<BranchChoice>(
                                        BranchChoice{left.last, child.index, childDie, k, rank})});
                                    if (frontier.size() > retainedPerState) frontier.pop_back();
                                }
                            }
                        }
                    }
                }
                current = std::move(next);
            }
            dp[v][die] = std::move(current);
        }
    }

    std::vector<F2fDieAssignment> result;
    for (int k = 1; k <= maxHbt; ++k) {
        std::vector<std::pair<int, int>> roots;
        for (int die = 0; die < 2; ++die) {
            for (int rank = 0; rank < static_cast<int>(dp[0][die][k].size()); ++rank) {
                roots.emplace_back(die, rank);
            }
        }
        auto cost = [&](const std::pair<int, int>& root) { return dp[0][root.first][k][root.second].cost; };
        std::stable_sort(roots.begin(), roots.end(), [&](const auto& a, const auto& b) {
            return cost(a) < cost(b);
        });
        if (roots.size() > retainedPerState) roots.resize(retainedPerState);
        for (const auto& root : roots) {
            F2fDieAssignment assignment{std::vector<int>(tree.size()), k, cost(root)};
            auto trace = [&](auto&& self, int node, int die, int count, int rank) -> void {
                assignment.labels[node] = die;
                for (auto choice = dp[node][die][count][rank].last; choice; choice = choice->previous) {
                    self(self, choice->child, choice->die, choice->hbt, choice->rank);
                }
            };
            trace(trace, 0, root.first, k, root.second);
            result.push_back(std::move(assignment));
        }
    }
    return result;
}
