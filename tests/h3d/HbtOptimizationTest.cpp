#include "h3d/HbtLocationOptimizer.h"
#include <algorithm>
#include <iostream>
#include <random>
#include <set>
#include <stdexcept>
#include <functional>

namespace {
using namespace h3d;

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

int direction(DBU value, const utils::IntervalT<DBU>& interval) {
    return value < interval.low ? -1 : value > interval.high ? 1 : 0;
}

void checkGrid() {
    std::vector<DBU> xs, ys;
    for (int x = 0; x < 17; x++) xs.push_back(x * 13);
    for (int y = 0; y < 11; y++) ys.push_back(y * 19);
    HbtGrid grid(xs, ys, {0, 23, 51});
    std::mt19937 random(7129);
    for (int trial = 0; trial < 200; trial++) {
        int site = random() % grid.size();
        if (grid.owner(site) == -1) grid.occupy({site, site}, 0);
        else if (grid.owner(site) == 0) grid.release({site}, 0);
        DBU a = static_cast<int>(random() % 300) - 40;
        DBU b = static_cast<int>(random() % 300) - 40;
        DBU c = static_cast<int>(random() % 300) - 40;
        DBU d = static_cast<int>(random() % 300) - 40;
        utils::BoxT<DBU> box(std::min(a, b), std::min(c, d), std::max(a, b), std::max(c, d));
        std::vector<int> inside;
        int available = 0;
        for (int i = 0; i < grid.size(); i++) {
            require(grid.siteAt(grid.point(i)) == i, "Site identity does not round trip");
            if (grid.owner(i) != -1) continue;
            available++;
            auto p = grid.point(i);
            if (direction(p.x, box.x) == 0 && direction(p.y, box.y) == 0) inside.push_back(i);
        }
        require(grid.available() == available, "Quadtree occupancy count is wrong");
        require(grid.within(box) == inside, "Quadtree range query differs from brute force");
        for (int dx = -1; dx <= 1; dx++) for (int dy = -1; dy <= 1; dy++) {
            if (!dx && !dy) continue;
            int expected = -1;
            DBU best = std::numeric_limits<DBU>::max();
            for (int i = 0; i < grid.size(); i++) {
                if (grid.owner(i) != -1) continue;
                auto p = grid.point(i);
                if (direction(p.x, box.x) != dx || direction(p.y, box.y) != dy) continue;
                DBU cost = utils::Dist(box, p);
                if (cost < best) { best = cost; expected = i; }
            }
            require(grid.nearest(box, dx, dy) == expected, "Directional query differs from brute force");
        }
    }
    HbtGrid ownership({0, 10}, {0, 10}, {3});
    ownership.occupy({0}, 7);
    try { ownership.occupy({1, 0}, 8); require(false, "Expected a conflicting owner"); }
    catch (const std::runtime_error&) { require(ownership.owner(1) == -1, "Failed claim partially modified ownership"); }
    try { ownership.release({0}, -1); require(false, "Expected an invalid release owner"); }
    catch (const std::invalid_argument&) {}
    require(ownership.owner(3) == -2, "Blocked site became available");

    // 物理 DBU 不应在区域查询中被截断成 32 位格子编号。
    const DBU offset = 5000000000LL;
    HbtGrid large({offset, offset + 10}, {offset, offset + 10});
    require(large.within({offset, offset, offset, offset}) == std::vector<int>({0}), "64-bit coordinate query was truncated");
}

void checkLocationDp() {
    HbtGrid hbt({0, 10, 20}, {0, 10, 20}, {4});
    HbtGrid routing({0, 10, 20}, {0, 10, 20});
    AssignedTopology topology;
    topology.tree.addNode({5, 5}); // HBT
    topology.tree.addNode({15, 15}); // ordinary Steiner point
    topology.tree.addNode({0, 0}, {{Die::Bottom, 0, 0}});
    topology.tree.addNode({20, 0}, {{Die::Top, 1, 0}});
    topology.tree.addNode({20, 20}, {{Die::Top, 2, 0}});
    topology.tree.addEdge(0, 2); topology.tree.addEdge(0, 1);
    topology.tree.addEdge(1, 3); topology.tree.addEdge(1, 4);
    topology.edgeDies = {Die::Bottom, Die::Top, Die::Top, Die::Top};
    topology.hbtNodes = {0};
    auto result = HbtLocationOptimizer().run(topology, hbt, routing);
    DBU expected = std::numeric_limits<DBU>::max();
    for (int h = 0; h < hbt.size(); h++) if (hbt.owner(h) == -1) {
        for (int s = 0; s < routing.size(); s++) {
            auto a = hbt.point(h), b = routing.point(s);
            DBU cost = utils::Dist(a, utils::PointT<DBU>(0, 0)) + utils::Dist(a, b)
                     + utils::Dist(b, utils::PointT<DBU>(20, 0)) + utils::Dist(b, utils::PointT<DBU>(20, 20));
            expected = std::min(expected, cost);
        }
    }
    require(result.wirelength == expected, "Position DP differs from exhaustive placement on the small grid");
    require(result.positions[2] == topology.tree.getNodes()[2].position, "Position DP moved a fixed pin");
    require(hbt.available() == 8, "Position scoring prematurely occupied HBT sites");

    H3DTree coincident;
    coincident.addNode({7, 8}, {{Die::Bottom, 0, 0}, {Die::Top, 1, 0}});
    auto expanded = expandAssignment(coincident, DieAssignment().run(coincident));
    HbtGrid oneSite({10}, {10});
    auto moved = HbtLocationOptimizer().run(expanded, oneSite, routing);
    require(moved.positions[0] == utils::PointT<DBU>(10, 10), "HBT was not moved to its legal physical site");
    require(moved.wirelength == 10, "Both die connections to the fixed pins must contribute wirelength");
    for (std::size_t i = 1; i < moved.positions.size(); i++) require(moved.positions[i] == utils::PointT<DBU>(7, 8), "A coincident pin moved with the HBT");

    AssignedTopology merge;
    merge.tree.addNode({0, 0}); merge.tree.addNode({20, 0});
    merge.tree.addNode({0, 0}, {{Die::Bottom, 0, 0}});
    merge.tree.addNode({20, 0}, {{Die::Bottom, 1, 0}});
    merge.tree.addEdge(0, 1); merge.tree.addEdge(0, 2); merge.tree.addEdge(1, 3);
    merge.edgeDies = {Die::Top, Die::Bottom, Die::Bottom}; merge.hbtNodes = {0, 1};
    auto merged = HbtLocationOptimizer().run(merge, oneSite, routing);
    require(merged.hbtSites == std::vector<int>({0, 0}), "Same-net HBT sharing was disallowed");
    oneSite.occupy(merged.hbtSites, 0);
    require(oneSite.available() == 0, "A merged HBT must occupy exactly one site");
    bool failed = false;
    try { HbtLocationOptimizer().run(merge, oneSite, routing); }
    catch (const std::runtime_error&) { failed = true; }
    require(failed, "An exhausted HBT array must not silently reuse an occupied site");
}

void checkCoincidentRepresentatives() {
    // Twenty distinct physical resources represented by fewer GCell positions.
    HbtGrid grid({0, 0, 10, 10, 20}, {0, 0, 10, 20}, {0}, true);
    require(grid.size() == 20 && grid.available() == 19, "Projection dropped physical HBT resources");
    std::mt19937 random(209);
    for (int trial = 0; trial < 40; trial++) {
        int site = random() % grid.size();
        if (grid.owner(site) == -1) grid.occupy({site}, 7);
        else if (grid.owner(site) == 7) grid.release({site}, 7);
        utils::BoxT<DBU> box(5, 5, 15, 15);
        std::vector<int> inside;
        int count = 0;
        for (int i = 0; i < grid.size(); i++) if (grid.owner(i) == -1) {
            count++;
            auto p = grid.point(i);
            if (direction(p.x, box.x) == 0 && direction(p.y, box.y) == 0) inside.push_back(i);
        }
        require(grid.available() == count && grid.within(box) == inside, "Coincident range/ownership accounting differs");
        for (int dx = -1; dx <= 1; dx++) for (int dy = -1; dy <= 1; dy++) {
            if (!dx && !dy) continue;
            DBU best = std::numeric_limits<DBU>::max();
            std::vector<int> wanted;
            for (int i = 0; i < grid.size(); i++) if (grid.owner(i) == -1) {
                auto p = grid.point(i);
                if (direction(p.x, box.x) != dx || direction(p.y, box.y) != dy) continue;
                DBU cost = utils::Dist(box, p);
                if (cost < best) { best = cost; wanted.clear(); }
                if (cost == best) wanted.push_back(i);
            }
            require(grid.nearestAll(box, dx, dy) == wanted, "Coincident directional query lost a legal site");
        }
    }
    HbtGrid resources({0, 0}, {0}, {}, true), routing({0}, {0});
    AssignedTopology tree;
    tree.tree.addNode({0, 0});
    tree.tree.addNode({0, 0}, {{Die::Bottom, 0, 0}});
    tree.tree.addNode({0, 0}, {{Die::Top, 1, 0}});
    tree.tree.addEdge(0, 1); tree.tree.addEdge(0, 2);
    tree.hbtNodes = {0}; tree.edgeDies = {Die::Bottom, Die::Top};
    auto first = HbtLocationOptimizer().runPaper(tree, resources, routing, 0, false);
    resources.occupy(first.hbtSites, 1);
    auto second = HbtLocationOptimizer().runPaper(tree, resources, routing, 0, false);
    require(first.hbtSites != second.hbtSites && first.positions == second.positions,
            "Two nets cannot independently use physical sites sharing an optimization point");
    resources.occupy(second.hbtSites, 2);
    require(resources.available() == 0, "Coincident sites were incorrectly deduplicated or double counted");
    bool exhausted = false;
    try { HbtLocationOptimizer().runPaper(tree, resources, routing, 0, false); }
    catch (const std::runtime_error&) { exhausted = true; }
    require(exhausted, "An exhausted coincident grid reused another net's physical resource");
}

DBU brutePlacement(const AssignedTopology& tree, const HbtGrid& hbt, const HbtGrid& routing, int maxSites) {
    const auto& nodes = tree.tree.getNodes();
    std::set<int> hbts(tree.hbtNodes.begin(), tree.hbtNodes.end());
    std::vector<int> variable;
    std::vector<std::vector<utils::PointT<DBU>>> domains(nodes.size());
    std::vector<utils::PointT<DBU>> positions;
    for (std::size_t i = 0; i < nodes.size(); i++) {
        positions.push_back(nodes[i].position);
        if (!nodes[i].pins.empty()) continue;
        variable.push_back(i);
        const auto& grid = hbts.count(i) ? hbt : routing;
        for (int site : grid.within(grid.bounds())) domains[i].push_back(grid.point(site));
    }
    DBU best = std::numeric_limits<DBU>::max();
    std::function<void(std::size_t)> visit = [&](std::size_t depth) {
        if (depth == variable.size()) {
            std::set<int> sites;
            for (int node : tree.hbtNodes) sites.insert(hbt.siteAt(positions[node]));
            if (maxSites > 0 && sites.size() > static_cast<std::size_t>(maxSites)) return;
            DBU cost = 0;
            for (const auto& edge : tree.tree.getEdges()) cost += utils::Dist(positions[edge.u], positions[edge.v]);
            best = std::min(best, cost);
            return;
        }
        int node = variable[depth];
        for (auto point : domains[node]) { positions[node] = point; visit(depth + 1); }
    };
    visit(0);
    return best;
}

void checkExactAndBudgetRepairs() {
    // 旧八方向裁剪反例：旧结果 60，两个实际空闲站点的正确最优值是 54。
    HbtGrid sparse({1, 7, 13}, {1, 7, 13}, {0, 1, 2, 3, 4, 7, 8});
    HbtGrid routing({0, 10, 20}, {0, 10, 20});
    AssignedTopology pruning;
    pruning.tree.addNode({0, 0});
    pruning.tree.addNode({0, 0}, {{Die::Bottom, 0, 0}});
    pruning.tree.addNode({0, 0}, {{Die::Top, 1, 0}});
    pruning.tree.addNode({20, 0}, {{Die::Top, 2, 0}});
    for (int i = 1; i < 4; i++) pruning.tree.addEdge(0, i);
    pruning.edgeDies = {Die::Bottom, Die::Top, Die::Top}; pruning.hbtNodes = {0};
    require(HbtLocationOptimizer().run(pruning, sparse, routing).wirelength == 54, "Candidate-pruning counterexample was not repaired");

    // 两个网、两个点的资源反例；预算为后续网保留一站点，而不增加任何容量。
    HbtGrid resources({0, 20}, {0});
    H3DTree first;
    first.addNode({0, 0}, {{Die::Bottom, 0, 0}, {Die::Top, 1, 0}});
    first.addNode({20, 0}, {{Die::Bottom, 2, 0}, {Die::Top, 3, 0}});
    first.addEdge(0, 1);
    auto expanded = expandAssignment(first, DieAssignment().run(first));
    auto firstResult = HbtLocationOptimizer().runWithSiteBudget(expanded, resources, routing, resources.available() - 1);
    require(firstResult.wirelength == brutePlacement(expanded, resources, routing, 1), "Single-site budget is not exact");
    resources.occupy(firstResult.hbtSites, 0);
    require(resources.available() == 1, "First net consumed the next net's reserve");
    H3DTree second;
    second.addNode({10, 0}, {{Die::Bottom, 4, 0}, {Die::Top, 5, 0}});
    auto secondTree = expandAssignment(second, DieAssignment().run(second));
    auto secondResult = HbtLocationOptimizer().runWithSiteBudget(secondTree, resources, routing, 1);
    resources.occupy(secondResult.hbtSites, 1);
    require(resources.available() == 0 && firstResult.hbtSites[0] != secondResult.hbtSites[0], "Two-net resource repair failed");

    H3DTree three;
    for (int i = 0; i < 3; i++) three.addNode({i * 10, 0}, {{Die::Bottom, i * 2, 0}, {Die::Top, i * 2 + 1, 0}});
    three.addEdge(0, 1); three.addEdge(1, 2);
    auto threeTree = expandAssignment(three, DieAssignment().run(three));
    HbtGrid threeSites({0, 10, 20}, {0});
    auto bounded = HbtLocationOptimizer().runWithSiteBudget(threeTree, threeSites, routing, 2);
    require(bounded.budgetFallback && bounded.siteBudget == 2 &&
            std::set<int>(bounded.hbtSites.begin(), bounded.hbtSites.end()).size() <= 2,
            "An over-budget unconstrained solution was not replaced by a feasible one");
    require(bounded.wirelength == brutePlacement(threeTree, threeSites, routing, 1), "Budget fallback did not compute the exact coalesced solution");

    // 多移动节点、非均匀坐标与离散空洞的随机穷举，验证距离变换和回溯。
    std::mt19937 random(98017);
    for (int sample = 0; sample < 80; sample++) {
        HbtGrid hbt({-3, 5, 14}, {-2, 11}, {static_cast<int>(random() % 6)});
        HbtGrid ordinary({-4, 6, 17}, {-5, 13}, sample % 2 ? std::vector<int>({2}) : std::vector<int>());
        AssignedTopology tree;
        tree.tree.addNode({0, 0}); tree.tree.addNode({0, 0}); tree.tree.addNode({0, 0});
        for (int i = 0; i < 3; i++) tree.tree.addNode({static_cast<int>(random() % 25) - 8, static_cast<int>(random() % 22) - 7},
                                                  {{static_cast<Die>(i % 2), i, 0}});
        for (auto edge : {std::make_pair(0, 1), std::make_pair(1, 2), std::make_pair(0, 3), std::make_pair(1, 4), std::make_pair(2, 5)}) {
            tree.tree.addEdge(edge.first, edge.second); tree.edgeDies.push_back(Die::Bottom);
        }
        tree.hbtNodes = {0, 2};
        auto exact = HbtLocationOptimizer().run(tree, hbt, ordinary);
        require(exact.wirelength == brutePlacement(tree, hbt, ordinary, 0), "Full-domain L1 tree DP differs from exhaustive placement");
        auto audit = HbtLocationOptimizer().runPaperFullDomainAudit(tree, hbt, ordinary, sample + 1);
        require(audit.wirelength == exact.wirelength, "Same-paper-forest full-domain control differs from exhaustive placement");
        auto single = HbtLocationOptimizer().runSingleSite(tree, hbt, ordinary);
        require(single.wirelength == brutePlacement(tree, hbt, ordinary, 1), "Single-site solver differs from exhaustive placement");
        require(std::set<int>(single.hbtSites.begin(), single.hbtSites.end()).size() == 1, "Single-site solver used multiple sites");
        for (std::size_t i = 3; i < 6; i++) require(exact.positions[i] == tree.tree.getNodes()[i].position &&
                                                 single.positions[i] == tree.tree.getNodes()[i].position, "Exact DP moved an original pin");
    }
}
} // namespace

int main() {
    try {
        checkGrid(); checkLocationDp(); checkExactAndBudgetRepairs(); checkCoincidentRepresentatives();
        std::cout << "HBT quadtree, ownership, placement DP, fixed pins, merge and exhaustion checks passed.\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n'; return 1;
    }
}
