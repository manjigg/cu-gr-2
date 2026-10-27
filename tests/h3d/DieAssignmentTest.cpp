// 独立验证：穷举所有边的 die 分配，检查 DP 最优值、回溯约束与重新选根的一致性。
#include "h3d/DieAssignment.h"

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <limits>
#include <random>
#include <stdexcept>
#include <string>

namespace {

using namespace h3d;

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

PinReference pin(Die die, int instance = 0) {
    return {die, instance, 0};
}

// 独立评分：直接读取每个节点的固定引脚和全部 incident edges，未使用 DP 状态。
std::vector<unsigned> connectionMasks(const H3DTree& tree, const std::vector<Die>& edgeDies) {
    std::vector<unsigned> masks(tree.getNodes().size(), 0);
    for (std::size_t i = 0; i < tree.getNodes().size(); i++) {
        for (const auto& ref : tree.getNodes()[i].pins) masks[i] |= 1u << static_cast<int>(ref.die);
    }
    for (std::size_t i = 0; i < tree.getEdges().size(); i++) {
        const auto& edge = tree.getEdges()[i];
        unsigned bit = 1u << static_cast<int>(edgeDies[i]);
        masks[edge.u] |= bit;
        masks[edge.v] |= bit;
    }
    return masks;
}

int exhaustiveMinimum(const H3DTree& tree) {
    std::size_t nEdges = tree.getEdges().size();
    require(nEdges < 20, "Exhaustive oracle is intended for small trees");
    int best = std::numeric_limits<int>::max();
    std::vector<Die> dies(nEdges);
    for (std::uint64_t assignment = 0; assignment < (std::uint64_t(1) << nEdges); assignment++) {
        for (std::size_t i = 0; i < nEdges; i++) dies[i] = static_cast<Die>((assignment >> i) & 1u);
        const auto masks = connectionMasks(tree, dies);
        int cost = static_cast<int>(std::count(masks.begin(), masks.end(), 3u));
        best = std::min(best, cost);
    }
    return best;
}

void checkResult(const H3DTree& tree, const DieAssignmentResult& result, int expected) {
    require(result.hbtCount == expected, "DP differs from the exhaustive minimum");
    require(result.edgeDies.size() == tree.getEdges().size(), "Missing edge assignments");
    require(result.nodeDieSpans.size() == tree.getNodes().size(), "Missing node spans");
    auto masks = connectionMasks(tree, result.edgeDies);
    std::vector<int> hbtNodes;
    for (std::size_t i = 0; i < masks.size(); i++) {
        const auto& span = result.nodeDieSpans[i];
        int low = static_cast<int>(span.low);
        int high = static_cast<int>(span.high);
        require(low >= 0 && high < 2 && low <= high, "Invalid die span");
        unsigned allowed = low == high ? (1u << low) : 3u;
        require((masks[i] & ~allowed) == 0, "Node span misses a pin or incident edge");
        require((low != high) == (masks[i] == 3u), "Traceback inserted an unnecessary HBT");
        if (masks[i] == 3u) hbtNodes.push_back(static_cast<int>(i));
    }
    auto reported = result.hbtNodes;
    std::sort(reported.begin(), reported.end());
    require(reported == hbtNodes, "HBT node list differs from the traced connections");
    require(static_cast<int>(reported.size()) == result.hbtCount, "HBT count differs from traceback");
}

void checkEveryRoot(const H3DTree& tree, int expected) {
    DieAssignment solver;
    auto first = solver.run(tree);
    checkResult(tree, first, expected);
    auto repeated = solver.run(tree);
    require(first.edgeDies == repeated.edgeDies && first.hbtNodes == repeated.hbtNodes,
            "Repeated runs must be deterministic");
    for (std::size_t root = 0; root < tree.getNodes().size(); root++) {
        auto result = solver.run(tree, static_cast<int>(root));
        require(result.root == static_cast<int>(root), "Explicit root was ignored");
        checkResult(tree, result, expected);
        checkResult(tree, solver.run(tree, static_cast<int>(root), Die::Top), expected);
        for (std::uint32_t seed : {1u, 19u}) checkResult(tree, solver.run(tree, static_cast<int>(root), Die::Bottom, seed), expected);
    }
}

template <typename Exception, typename Function>
void expectException(Function function) {
    bool caught = false;
    try {
        function();
    } catch (const Exception&) {
        caught = true;
    }
    require(caught, "Expected an exception for invalid topology input");
}

void checkExamples() {
    H3DTree empty;
    checkEveryRoot(empty, 0);
    require(DieAssignment().run(empty).root == -1, "Empty tree must have no root");

    H3DTree samePosition;
    samePosition.addNode({100, 200}, {pin(Die::Bottom), pin(Die::Top, 1)});
    checkEveryRoot(samePosition, 1);

    H3DTree singleDie;
    singleDie.addNode({0, 0}, {pin(Die::Top)});
    singleDie.addNode({1, 0});
    singleDie.addNode({2, 0}, {pin(Die::Top, 1)});
    singleDie.addEdge(0, 1);
    singleDie.addEdge(1, 2);
    checkEveryRoot(singleDie, 0);

    // 两个 Top 分支共用同一个 HBT，而不是按跨 die 分支数计为两个。
    H3DTree branching;
    branching.addNode({0, 0});
    branching.addNode({-1, 0}, {pin(Die::Bottom)});
    branching.addNode({1, 0}, {pin(Die::Top, 1)});
    branching.addNode({0, 1}, {pin(Die::Top, 2)});
    branching.addEdge(0, 1);
    branching.addEdge(0, 2);
    branching.addEdge(0, 3);
    checkEveryRoot(branching, 1);

    // 固定拓扑可要求多个 HBT：B--T--B--T，不能把每个跨 die 网一律计为一个。
    H3DTree alternating;
    for (int i = 0; i < 4; i++) alternating.addNode({i, 0}, {pin(static_cast<Die>(i % 2), i)});
    for (int i = 1; i < 4; i++) alternating.addEdge(i - 1, i);
    checkEveryRoot(alternating, 2);
}

void checkRandomTrees() {
    std::mt19937 generator(20261016);
    for (int size = 1; size <= 9; size++) {
        for (int sample = 0; sample < 100; sample++) {
            H3DTree tree;
            for (int i = 0; i < size; i++) {
                std::vector<PinReference> pins;
                unsigned mask = generator() % 4;
                if (mask & 1u) pins.push_back(pin(Die::Bottom, i));
                if (mask & 2u) pins.push_back(pin(Die::Top, i));
                tree.addNode({i, static_cast<int>(generator() % 3)}, pins);
            }
            for (int i = 1; i < size; i++) tree.addEdge(i, static_cast<int>(generator() % i));
            checkEveryRoot(tree, exhaustiveMinimum(tree));
        }
    }

    // 高度数节点和长链检查；遍历与 DP 不依赖论文示例中的小度数或递归栈深度。
    H3DTree star;
    star.addNode({0, 0});
    for (int i = 1; i <= 100; i++) {
        star.addNode({i, 0}, {pin(static_cast<Die>(i % 2), i)});
        star.addEdge(0, i);
    }
    checkEveryRoot(star, 1);

    H3DTree chain;
    const int length = 10000;
    for (int i = 0; i < length; i++) {
        std::vector<PinReference> pins;
        if (i == 0) pins.push_back(pin(Die::Bottom));
        if (i == length - 1) pins.push_back(pin(Die::Top, 1));
        chain.addNode({i, 0}, pins);
        if (i > 0) chain.addEdge(i - 1, i);
    }
    checkResult(chain, DieAssignment().run(chain), 1);
}

void checkInvalidInputs() {
    H3DTree tree;
    tree.addNode({0, 0});
    expectException<std::out_of_range>([&] { tree.addEdge(0, 1); });
    expectException<std::invalid_argument>([&] { tree.addEdge(0, 0); });
    expectException<std::out_of_range>([&] { tree.rootAt(1); });
    expectException<std::out_of_range>([&] { tree.rootAt(-2); });
    expectException<std::invalid_argument>([&] { tree.addNode({0, 0}, {pin(static_cast<Die>(2))}); });
    tree.addNode({1, 0});
    expectException<std::invalid_argument>([&] { DieAssignment().run(tree); });

    H3DTree cycle;
    for (int i = 0; i < 4; i++) cycle.addNode({i, 0});
    cycle.addEdge(0, 1);
    cycle.addEdge(1, 2);
    cycle.addEdge(2, 0);
    expectException<std::invalid_argument>([&] { cycle.rootAt(0); });
    expectException<std::invalid_argument>([&] { cycle.rootAt(3); });

    H3DTree parallel;
    for (int i = 0; i < 3; i++) parallel.addNode({i, 0});
    parallel.addEdge(0, 1);
    parallel.addEdge(0, 1);
    expectException<std::invalid_argument>([&] { parallel.rootAt(0); });
}

} // namespace

int main() {
    try {
        checkExamples();
        checkRandomTrees();
        checkInvalidInputs();
        std::cout << "H3D die assignment: exhaustive, traceback, root, and invalid-input checks passed.\n";
    } catch (const std::exception& exception) {
        std::cerr << exception.what() << '\n';
        return 1;
    }
    return 0;
}
