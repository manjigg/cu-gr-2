#include "F2fCandidateSearch.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <stdexcept>

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

int f2fDieAssignmentHbtUpperBound(const std::vector<F2fLabelNode>& tree) {
    if (tree.empty()) return 0;

    long long bound = 0;
    for (const auto& node : tree) {
        const int local = std::max(node.localHbt[0], node.localHbt[1]);
        if (local < 0) {
            throw std::invalid_argument("Negative local HBT contribution in F2F tree");
        }
        bound += local;
        bound += static_cast<long long>(node.children.size());
        if (bound > std::numeric_limits<int>::max()) {
            throw std::overflow_error("F2F structural HBT upper bound exceeds int range");
        }
    }
    return static_cast<int>(bound);
}

std::vector<F2fDieAssignment> searchF2fDieAssignments(
    const std::vector<F2fLabelNode>& tree) {
    if (tree.empty()) return {};

    const int maxHbt = f2fDieAssignmentHbtUpperBound(tree);
    if (maxHbt < 1) return {};

    std::vector<std::array<Frontier, 2>> dp(tree.size());
    for (int v = static_cast<int>(tree.size()) - 1; v >= 0; --v) {
        for (int die = 0; die < 2; ++die) {
            Frontier current(maxHbt + 1);
            const int local = tree[v].localHbt[die];
            if (local < 0 || local > maxHbt) {
                throw std::logic_error("Invalid local HBT contribution in F2F DP");
            }
            if (std::isfinite(tree[v].localCost[die])) {
                current[local].push_back({tree[v].localCost[die], nullptr});
            }

            // Tree-knapsack convolution. k includes child-subtree HBTs,
            // parent-child Die crossings, and local opposite-Die terminals.
            for (const auto& child : tree[v].children) {
                if (child.index <= v || child.index >= static_cast<int>(tree.size())) {
                    throw std::logic_error("F2F projected tree must be parent-before-child");
                }

                Frontier next(maxHbt + 1);
                for (int used = 0; used <= maxHbt; ++used) {
                    for (const State& left : current[used]) {
                        for (int childDie = 0; childDie < 2; ++childDie) {
                            const int base = used + (childDie != die);
                            if (base > maxHbt) continue;

                            for (int k = 0; k + base <= maxHbt; ++k) {
                                const auto& right = dp[child.index][childDie][k];
                                for (int rank = 0; rank < static_cast<int>(right.size()); ++rank) {
                                    const double cost =
                                        left.cost + child.cost[die][childDie] + right[rank].cost;
                                    auto& frontier = next[base + k];
                                    if (!std::isfinite(cost) ||
                                        (frontier.size() == retainedPerState &&
                                         cost >= frontier.back().cost)) {
                                        continue;
                                    }

                                    auto pos = std::upper_bound(
                                        frontier.begin(), frontier.end(), cost,
                                        [](double value, const State& state) {
                                            return value < state.cost;
                                        });
                                    frontier.insert(
                                        pos,
                                        State{cost, std::make_shared<BranchChoice>(
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

        auto cost = [&](const std::pair<int, int>& root) {
            return dp[0][root.first][k][root.second].cost;
        };
        std::stable_sort(roots.begin(), roots.end(), [&](const auto& a, const auto& b) {
            return cost(a) < cost(b);
        });
        if (roots.size() > retainedPerState) roots.resize(retainedPerState);

        for (const auto& root : roots) {
            F2fDieAssignment assignment{std::vector<int>(tree.size()), k, cost(root)};
            auto trace = [&](auto&& self, int node, int die, int count, int rank) -> void {
                assignment.labels[node] = die;
                for (auto choice = dp[node][die][count][rank].last;
                     choice; choice = choice->previous) {
                    self(self, choice->child, choice->die, choice->hbt, choice->rank);
                }
            };
            trace(trace, 0, root.first, k, root.second);
            result.push_back(std::move(assignment));
        }
    }
    return result;
}
