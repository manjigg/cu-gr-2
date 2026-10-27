#include "h3d/SubnetPartition.h"
#include <algorithm>
#include <iostream>
#include <set>
#include <stdexcept>

namespace {
using namespace h3d;
void require(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }

void rootsAndTies() {
    H3DTree pair;
    pair.addNode({0, 0}, {{Die::Top, 0, 0}});
    pair.addNode({20, 0}, {{Die::Bottom, 1, 0}});
    pair.addEdge(0, 1);
    auto lower = DieAssignment().run(pair, 0, Die::Bottom);
    auto upper = DieAssignment().run(pair, 0, Die::Top);
    require(lower.hbtCount == 1 && upper.hbtCount == 1 && lower.edgeDies == std::vector<Die>({Die::Bottom}) &&
            upper.edgeDies == std::vector<Die>({Die::Top}) && lower.hbtNodes != upper.hbtNodes,
            "Mirrored die tie selection changed the count optimum or failed to expose an equivalent assignment");
    H3DTree star;
    star.addNode({0, 0});
    star.addNode({-10, 0}, {{Die::Bottom, 0, 0}});
    star.addNode({10, 0}, {{Die::Top, 1, 0}});
    star.addNode({0, 10}, {{Die::Top, 2, 0}});
    for (int i = 1; i <= 3; i++) star.addEdge(0, i);
    std::mt19937 a(19), b(19);
    for (int trial = 0; trial < 32; trial++) {
        int root = chooseLeafRoot(star, a);
        require(root >= 1 && root <= 3 && root == chooseLeafRoot(star, b), "Paper root is not a reproducible leaf");
        require(DieAssignment().run(star, root).hbtCount == 1, "Paper root changed the minimum HBT count");
    }
    HbtGrid ties({0, 10, 20}, {0, 10, 20}, {0, 1, 3, 5, 7, 8});
    require(ties.nearestAll({-1, -1}, 1, 1) == std::vector<int>({2, 4, 6}), "Directional nearest ties were discarded");
    require(ties.nearest({-1, -1}, 1, 1) == 2, "Legacy single-nearest tie rule changed");
    std::mt19937 boxes(911);
    for (int i = 0; i < 256; i++) {
        int x = static_cast<int>(boxes() % 100) - 50, y = static_cast<int>(boxes() % 100) - 50;
        utils::BoxT<DBU> box(x, y, x + static_cast<int>(boxes() % 10), y + static_cast<int>(boxes() % 10));
        auto available = ties.within(box);
        for (int dx = -1; dx <= 1; dx++) for (int dy = -1; dy <= 1; dy++) if (dx || dy) {
            auto more = ties.nearestAll(box, dx, dy); available.insert(available.end(), more.begin(), more.end());
        }
        require(!available.empty(), "Eight directional sectors lost all candidates while legal sites remain");
    }
}

void candidateFormulaAndForest() {
    HbtGrid sparse({1, 7, 13}, {1, 7, 13}, {0, 1, 2, 3, 4, 7, 8});
    HbtGrid routing({0, 10, 20}, {0, 10, 20});
    AssignedTopology tree;
    tree.tree.addNode({0, 0});
    tree.tree.addNode({0, 0}, {{Die::Bottom, 0, 0}});
    tree.tree.addNode({0, 0}, {{Die::Top, 1, 0}});
    tree.tree.addNode({20, 0}, {{Die::Top, 2, 0}});
    for (int i = 1; i <= 3; i++) tree.tree.addEdge(0, i);
    tree.edgeDies = {Die::Bottom, Die::Top, Die::Top}; tree.hbtNodes = {0};
    auto paper = HbtLocationOptimizer().runPaper(tree, sparse, routing, 0);
    require(paper.paperNodes.size() == 1 && paper.paperNodes[0].region == utils::BoxT<DBU>(0, 0), "Equation-6 median region is wrong");
    require(paper.paperNodes[0].candidateSites == std::vector<int>({6}), "Paper candidate union was replaced by full-domain search");
    require(paper.wirelength == 60 && HbtLocationOptimizer().run(tree, sparse, routing).wirelength == 54,
             "Paper candidate mode silently used the exact enhancement");
    auto fullAudit = HbtLocationOptimizer().runPaperFullDomainAudit(tree, sparse, routing, 0);
    require(fullAudit.wirelength == 54 && fullAudit.paperRoots == paper.paperRoots && fullAudit.paperBfsOrder == paper.paperBfsOrder &&
            fullAudit.mode == "diagnostic_full_domain_same_paper_forest", "Candidate-only control changed the forest or was labeled as paper output");
    HbtGrid empty({0}, {0}, {0});
    bool resourceFailure = false;
    try { HbtLocationOptimizer().runPaper(tree, empty, routing); }
    catch (const HbtResourceExhausted&) { resourceFailure = true; }
    require(resourceFailure, "An exhausted grid was not distinguished from a routing/path failure");
    require(paper.siteBudget == -1 && !paper.budgetFallback && sparse.available() == 2,
            "Paper scoring applied a site budget or prematurely committed resources");
    auto again = HbtLocationOptimizer().runPaper(tree, sparse, routing, 0);
    require(paper.positions == again.positions && paper.paperBfsOrder == again.paperBfsOrder, "Paper location seed is not reproducible");

    AssignedTopology forest;
    forest.tree.addNode({0, 0});
    forest.tree.addNode({10, 0}, {{Die::Bottom, 0, 0}});
    forest.tree.addNode({20, 0});
    forest.tree.addNode({0, 10}, {{Die::Top, 1, 0}});
    forest.tree.addNode({20, 10}, {{Die::Top, 2, 0}});
    for (auto edge : {std::make_pair(0, 1), std::make_pair(1, 2), std::make_pair(0, 3), std::make_pair(2, 4)}) forest.tree.addEdge(edge.first, edge.second);
    forest.edgeDies = {Die::Bottom, Die::Bottom, Die::Top, Die::Top}; forest.hbtNodes = {0, 2};
    HbtGrid full({0, 10, 20}, {0, 10, 20});
    auto solved = HbtLocationOptimizer().runPaper(forest, full, routing, 71);
    require(std::set<int>(solved.paperRoots.begin(), solved.paperRoots.end()) == std::set<int>({0, 2}), "BFS crossed a fixed pin instead of forming a forest");
    require(solved.continuousWirelength == 40 && solved.wirelength == 40, "Continuous region/trace solution cost is wrong");
    for (const auto& node : solved.paperNodes) {
        require(node.children.size() == 2 && std::find(node.children.begin(), node.children.end(), 1) != node.children.end(), "Fixed pin is not a boundary leaf");
        require(node.region == (node.node == 0 ? utils::BoxT<DBU>(0, 0, 10, 10) : utils::BoxT<DBU>(10, 0, 20, 10)), "Forest median box is wrong");
        for (std::size_t i = 0; i < node.candidateSites.size(); i++) {
            DBU expected = 0;
            for (int child : node.children) expected += utils::Dist(full.point(node.candidateSites[i]), forest.tree.getNodes()[child].position);
            require(node.costs[i] == expected, "Equation-7 table differs from direct pin-distance enumeration");
        }
    }
}

void reverseDfsSplit() {
    H3DTree original;
    original.addNode({0, 0}, {{Die::Bottom, 0, 0}, {Die::Top, 1, 0}});
    original.addNode({20, 0}, {{Die::Bottom, 2, 0}, {Die::Top, 3, 0}});
    original.addEdge(0, 1);
    auto assigned = expandAssignment(original, DieAssignment().run(original));
    LocationResult positions;
    for (const auto& node : assigned.tree.getNodes()) positions.positions.push_back(node.position);
    positions.hbtSites = {0, 1};
    auto split = partitionAtHbts(assigned, positions);
    require(split.components.size() == 3, "Reverse-DFS split did not separate die copies at HBTs");
    auto rooted = assigned.tree.rootAt(assigned.root);
    std::vector<int> order(assigned.tree.getNodes().size());
    for (std::size_t i = 0; i < split.reverseDfsOrder.size(); i++) order[split.reverseDfsOrder[i]] = i;
    for (int node : rooted.preorder) if (rooted.parents[node] != -1) require(order[node] < order[rooted.parents[node]], "Split is not bottom-up");
    positions.hbtSites = {0, 0}; positions.positions[0] = positions.positions[1] = {10, 0};
    split = partitionAtHbts(assigned, positions);
    require(split.components.size() == 2, "Same-net same-site HBTs were not merged before splitting");
    int pins = 0;
    for (const auto& part : split.components) {
        require(part.hbtSites == std::vector<int>({0}), "Merged HBT terminal is duplicated");
        for (int node : part.nodes) for (const auto& pin : assigned.tree.getNodes()[node].pins) if (pin.die == part.die) pins++;
    }
    require(pins == 4, "Split lost or duplicated original pins");

    AssignedTopology coincident;
    coincident.tree.addNode({0, 10}); coincident.tree.addNode({10, 10});
    coincident.tree.addNode({5, 0}); coincident.tree.addNode({0, 0}, {{Die::Bottom, 0, 0}});
    coincident.tree.addNode({5, 0}); coincident.tree.addNode({10, 0}, {{Die::Bottom, 1, 0}});
    coincident.tree.addNode({0, 20}, {{Die::Top, 2, 0}});
    for (auto edge : {std::make_pair(0, 1), std::make_pair(0, 2), std::make_pair(2, 3), std::make_pair(1, 4), std::make_pair(4, 5), std::make_pair(0, 6)}) coincident.tree.addEdge(edge.first, edge.second);
    coincident.edgeDies = {Die::Top, Die::Bottom, Die::Bottom, Die::Bottom, Die::Bottom, Die::Top}; coincident.hbtNodes = {0, 1};
    LocationResult fixed;
    for (const auto& node : coincident.tree.getNodes()) fixed.positions.push_back(node.position);
    fixed.hbtSites = {0, 1};
    auto kept = partitionAtHbts(coincident, fixed);
    require(kept.components.size() == 3, "Ordinary coincident points changed the fixed-topology HBT split");
    for (const auto& part : kept.components) if (part.die == Die::Bottom) {
        bool two = std::find(part.nodes.begin(), part.nodes.end(), 2) != part.nodes.end();
        bool four = std::find(part.nodes.begin(), part.nodes.end(), 4) != part.nodes.end();
        require(!(two && four), "Distinct bottom components were silently connected by coordinate equality");
    }
}

void zeroCostCoalescing() {
    AssignedTopology forest;
    forest.tree.addNode({0, 0}); forest.tree.addNode({20, 0});
    forest.tree.addNode({10, 0}, {{Die::Bottom, 0, 0}});
    forest.tree.addNode({0, 0}, {{Die::Top, 1, 0}});
    forest.tree.addNode({20, 0}, {{Die::Top, 2, 0}});
    for (auto edge : {std::make_pair(0, 2), std::make_pair(2, 1), std::make_pair(0, 3), std::make_pair(1, 4)}) forest.tree.addEdge(edge.first, edge.second);
    forest.edgeDies = {Die::Bottom, Die::Bottom, Die::Top, Die::Top}; forest.hbtNodes = {0, 1};
    HbtGrid grid({0, 10, 20}, {0});
    auto original = HbtLocationOptimizer().runPaper(forest, grid, grid, 0, false);
    auto merged = HbtLocationOptimizer().runPaper(forest, grid, grid, 0, true);
    require(original.wirelength == 20 && merged.wirelength == original.wirelength, "Tie coalescing increased the paper wirelength");
    require(std::set<int>(original.hbtSites.begin(), original.hbtSites.end()).size() == 2 && merged.hbtSites == std::vector<int>({1, 1}), "Equal-cost common site was not selected");
    require(merged.paperSitesBeforeTieBreak == 2 && merged.paperTieTrials > 0 && grid.available() == 3, "Tie scoring mutated site ownership or lost audit state");
    require(original.paperRoots == merged.paperRoots && original.paperBfsOrder == merged.paperBfsOrder, "Coalescing changed the paper forest");
    for (std::size_t i = 0; i < original.paperNodes.size(); i++) {
        require(original.paperNodes[i].candidateSites == merged.paperNodes[i].candidateSites &&
                original.paperNodes[i].costs == merged.paperNodes[i].costs && original.paperNodes[i].region == merged.paperNodes[i].region,
                "Coalescing changed the original Algorithm-1 candidate set or Equation-7 costs");
    }
    // Both regions now have unique, different minimizers. Sharing costs more,
    // so a tie policy must retain the original two-site solution.
    forest.tree.addNode({0, 0}, {{Die::Top, 3, 0}}); forest.tree.addEdge(0, 5); forest.edgeDies.push_back(Die::Top);
    forest.tree.addNode({20, 0}, {{Die::Top, 4, 0}}); forest.tree.addEdge(1, 6); forest.edgeDies.push_back(Die::Top);
    original = HbtLocationOptimizer().runPaper(forest, grid, grid, 0, false);
    merged = HbtLocationOptimizer().runPaper(forest, grid, grid, 0, true);
    require(merged.wirelength == original.wirelength && merged.hbtSites == original.hbtSites &&
            std::set<int>(merged.hbtSites.begin(), merged.hbtSites.end()).size() == 2,
            "Tie policy forced a non-optimal merge");

    AssignedTopology partial;
    for (int x : {0, 20, 40}) partial.tree.addNode({x, 0});
    partial.tree.addNode({10, 0}, {{Die::Bottom, 0, 0}});
    partial.tree.addNode({0, 0}, {{Die::Top, 1, 0}});
    partial.tree.addNode({20, 0}, {{Die::Top, 2, 0}});
    partial.tree.addNode({40, 0}, {{Die::Top, 3, 0}});
    partial.tree.addNode({40, 0}, {{Die::Top, 4, 0}});
    for (int i = 0; i < 3; i++) { partial.tree.addEdge(i, 3); partial.edgeDies.push_back(Die::Bottom); }
    for (auto edge : {std::make_pair(0, 4), std::make_pair(1, 5), std::make_pair(2, 6), std::make_pair(2, 7)}) {
        partial.tree.addEdge(edge.first, edge.second); partial.edgeDies.push_back(Die::Top);
    }
    partial.hbtNodes = {0, 1, 2};
    HbtGrid wider({0, 10, 20, 30, 40}, {0});
    original = HbtLocationOptimizer().runPaper(partial, wider, wider, 0, false);
    merged = HbtLocationOptimizer().runPaper(partial, wider, wider, 0, true);
    require(original.wirelength == 50 && merged.wirelength == 50 && merged.paperSitesBeforeTieBreak == 3 &&
            std::set<int>(merged.hbtSites.begin(), merged.hbtSites.end()).size() == 2,
            "Partial coalescing missed a zero-cost three-to-two-site merge");
    require(merged.paperPairTieTrials > 0 && merged.paperPairTieTrials <= 256 && merged.paperTieMerges.size() == 1 &&
            merged.paperTieMerges[0].forcedNodes.size() == 2, "Partial merge audit is incomplete");
}

void equivalentAssignmentTrials() {
    H3DTree tree;
    for (int i = 0; i < 4; i++) tree.addNode({i * 10, 0}, {{static_cast<Die>(i % 2), i, 0}});
    for (int i = 1; i < 4; i++) tree.addEdge(i - 1, i);
    auto initial = DieAssignment().run(tree, 0);
    HbtGrid grid({0, 10, 20, 30}, {0});
    HbtLocationOptimizer solver;
    auto single = solver.runPaperAssignments(tree, initial, grid, grid, 29, true, 1);
    auto original = solver.runPaper(expandAssignment(tree, initial), grid, grid, 29);
    require(single.trials.size() == 1 && single.selectedTrial == 0 && single.location.positions == original.positions,
            "Single assignment trial changed the baseline paper result");
    auto searched = solver.runPaperAssignments(tree, initial, grid, grid, 29, true, 32);
    auto again = solver.runPaperAssignments(tree, initial, grid, grid, 29, true, 32);
    require(searched.assignment.hbtCount == 2 && searched.selectedTrial == again.selectedTrial &&
            searched.location.positions == again.location.positions && searched.trials.size() == again.trials.size() && grid.available() == 4,
            "Equivalent quantity assignment search is not reproducible or changed count/ownership");
    const auto& chosen = searched.trials[searched.selectedTrial];
    for (const auto& trial : searched.trials) {
        require(trial.hbtNodes.size() == 2, "A non-minimum quantity assignment entered the search");
        require(chosen.physicalSites < trial.physicalSites ||
                (chosen.physicalSites == trial.physicalSites && chosen.wirelength <= trial.wirelength),
                "Search did not select the declared physical-site/wirelength ordering");
    }
}

void scarcityIsNotCandidatePruning() {
    // HPWL order prescribed by III-A: first net spans 20, second spans 60.
    // Both available sites occur in every relevant paper candidate set.
    H3DTree first;
    first.addNode({20, 0}, {{Die::Bottom, 0, 0}, {Die::Top, 1, 0}});
    first.addNode({40, 0}, {{Die::Bottom, 2, 0}, {Die::Top, 3, 0}});
    first.addEdge(0, 1);
    auto firstTree = expandAssignment(first, DieAssignment().run(first, 0));
    H3DTree second;
    second.addNode({0, 0}, {{Die::Bottom, 4, 0}});
    second.addNode({60, 0}, {{Die::Top, 5, 0}});
    second.addEdge(0, 1);
    auto secondTree = expandAssignment(second, DieAssignment().run(second, 0));
    HbtGrid sites({20, 40}, {0}), routing({0, 20, 40, 60}, {0});
    auto paper = HbtLocationOptimizer().runPaper(firstTree, sites, routing, 0, false);
    auto full = HbtLocationOptimizer().runPaperFullDomainAudit(firstTree, sites, routing, 0);
    require(paper.wirelength == 20 && full.wirelength == 20 &&
            std::set<int>(paper.hbtSites.begin(), paper.hbtSites.end()).size() == 2 && paper.hbtSites == full.hbtSites,
            "Controlled scarcity example changed when candidate pruning was disabled");
    for (const auto& node : paper.paperNodes) require(node.candidateSites == std::vector<int>({0, 1}),
            "Scarcity counterexample did not retain all legal candidates");
    DBU allMin = std::numeric_limits<DBU>::max(), commonMin = allMin;
    for (DBU x : {20, 40}) for (DBU y : {20, 40}) {
        DBU cost = 2 * std::abs(x - 20) + std::abs(x - y) + 2 * std::abs(y - 40);
        allMin = std::min(allMin, cost);
        if (x == y) commonMin = std::min(commonMin, cost);
    }
    require(allMin == 20 && commonMin == 40, "Independent enumeration did not establish the strict wirelength/resource tradeoff");
    sites.occupy(paper.hbtSites, 0);
    bool paperFailed = false, fullFailed = false;
    try { HbtLocationOptimizer().runPaper(secondTree, sites, routing); } catch (const HbtResourceExhausted&) { paperFailed = true; }
    try { HbtLocationOptimizer().runPaperFullDomainAudit(secondTree, sites, routing); } catch (const HbtResourceExhausted&) { fullFailed = true; }
    require(paperFailed && fullFailed, "Pruned and full domains did not share the same exhausted-resource failure");
    sites.release(paper.hbtSites, 0);
    // A feasible allocation exists, but it must abandon the first net's unique
    // local minimum. This is a witness, not the paper production policy.
    sites.occupy({0}, 0);
    auto later = HbtLocationOptimizer().runPaper(secondTree, sites, routing);
    require(later.hbtSites == std::vector<int>({1}) && later.wirelength == 60, "The same-grid feasible witness failed");
}
}

int main() {
    try {
        rootsAndTies(); candidateFormulaAndForest(); reverseDfsSplit(); zeroCostCoalescing(); equivalentAssignmentTrials(); scarcityIsNotCandidatePruning();
        std::cout << "Paper leaf roots, nearest ties, Eq6/Eq7, fixed-pin BFS forest and reverse-DFS HBT splitting passed.\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
