#include "HbtLocationOptimizer.h"
#include <algorithm>
#include <limits>
#include <queue>
#include <stdexcept>
#include <map>
#include <set>
#include <random>
#include <iterator>
#include <memory>

namespace h3d {

AssignedTopology expandAssignment(const H3DTree& topology, const DieAssignmentResult& assignment) {
    AssignedTopology result;
    if (assignment.edgeDies.size() != topology.getEdges().size() ||
        assignment.nodeDieSpans.size() != topology.getNodes().size()) {
        throw std::invalid_argument("Die assignment does not match the topology");
    }
    result.edgeDies = assignment.edgeDies;
    result.hbtNodes = assignment.hbtNodes;
    result.root = assignment.root;
    std::vector<bool> isHbt(topology.getNodes().size(), false);
    for (int node : result.hbtNodes) isHbt.at(node) = true;
    for (std::size_t i = 0; i < topology.getNodes().size(); i++) {
        const auto& node = topology.getNodes()[i];
        result.tree.addNode(node.position, isHbt[i] ? std::vector<PinReference>() : node.pins);
    }
    for (const auto& edge : topology.getEdges()) result.tree.addEdge(edge.u, edge.v);
    for (int index : result.hbtNodes) {
        const auto& node = topology.getNodes()[index];
        for (Die die : {Die::Bottom, Die::Top}) {
            std::vector<PinReference> pins;
            for (const auto& pin : node.pins) if (pin.die == die) pins.push_back(pin);
            if (pins.empty()) continue;
            int fixed = result.tree.addNode(node.position, pins);
            result.tree.addEdge(index, fixed);
            result.edgeDies.push_back(die);
        }
    }
    return result;
}

namespace {
constexpr DBU infinity = std::numeric_limits<DBU>::max() / 4;

DBU add(DBU a, DBU b) {
    if (a == infinity || b == infinity) return infinity;
    if (b < 0 || a > infinity - b) throw std::overflow_error("L1 placement cost exceeds its integer range");
    return a + b;
}

std::vector<bool> hbtMask(const AssignedTopology& topology) {
    topology.tree.rootAt();
    std::vector<bool> result(topology.tree.getNodes().size(), false);
    for (int node : topology.hbtNodes) {
        if (result.at(node)) throw std::invalid_argument("Duplicate topological HBT index");
        result[node] = true;
        if (!topology.tree.getNodes()[node].pins.empty()) throw std::invalid_argument("Expand fixed pins before moving an HBT");
    }
    return result;
}

void finish(LocationResult& result, const AssignedTopology& topology, const HbtGrid& hbt,
            const std::vector<int>* chosenSites = nullptr) {
    result.hbtSites.clear(); result.wirelength = 0;
    if (chosenSites && chosenSites->size() != topology.hbtNodes.size()) throw std::logic_error("HBT site traceback size differs");
    for (std::size_t i = 0; i < topology.hbtNodes.size(); i++) {
        int node = topology.hbtNodes[i];
        int site = chosenSites ? chosenSites->at(i) : hbt.availableSiteAt(result.positions.at(node));
        if (site < 0 || hbt.owner(site) != -1 || hbt.point(site) != result.positions.at(node)) throw std::runtime_error("Illegal HBT traceback");
        result.hbtSites.push_back(site);
    }
    for (const auto& edge : topology.tree.getEdges()) {
        result.wirelength = add(result.wirelength, utils::Dist(result.positions[edge.u], result.positions[edge.v]));
    }
}

struct Domain {
    std::vector<utils::PointT<DBU>> points;
    std::vector<int> indices;
};

// 在 routing/HBT/固定引脚的坐标并集上做精确 L1 min-plus 距离变换。
// 只有合法域位置作为种子；中间格点不增加候选资源或绕过占用约束。
struct L1Grid {
    std::vector<DBU> xs, ys;
    int width, height;
    L1Grid(const HbtGrid& hbt, const HbtGrid& routing, const H3DTree& tree) {
        xs = hbt.xCoordinates(); ys = hbt.yCoordinates();
        xs.insert(xs.end(), routing.xCoordinates().begin(), routing.xCoordinates().end());
        ys.insert(ys.end(), routing.yCoordinates().begin(), routing.yCoordinates().end());
        for (const auto& node : tree.getNodes()) if (!node.pins.empty()) {
            xs.push_back(node.position.x); ys.push_back(node.position.y);
        }
        for (auto* axis : {&xs, &ys}) {
            std::sort(axis->begin(), axis->end());
            axis->erase(std::unique(axis->begin(), axis->end()), axis->end());
        }
        if (xs.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()) / ys.size()) {
            throw std::length_error("L1 coordinate union is too large");
        }
        width = xs.size(); height = ys.size();
    }
    int index(utils::PointT<DBU> point) const {
        int x = std::lower_bound(xs.begin(), xs.end(), point.x) - xs.begin();
        int y = std::lower_bound(ys.begin(), ys.end(), point.y) - ys.begin();
        return y * width + x;
    }
    void indexDomain(Domain& domain) const {
        for (auto point : domain.points) domain.indices.push_back(index(point));
    }
    struct Message { std::vector<DBU> costs; std::vector<int> choices; };
    Message transform(const Domain& domain, const std::vector<DBU>& costs) const {
        Message result;
        result.costs.assign(static_cast<std::size_t>(width) * height, infinity);
        result.choices.assign(result.costs.size(), -1);
        for (std::size_t i = 0; i < costs.size(); i++) {
            result.costs[domain.indices[i]] = costs[i]; result.choices[domain.indices[i]] = i;
        }
        auto relax = [&](int target, int source, DBU distance) {
            if (result.choices[source] == -1) return;
            DBU value = add(result.costs[source], distance);
            if (value < result.costs[target] || (value == result.costs[target] &&
                (result.choices[target] == -1 || result.choices[source] < result.choices[target]))) {
                result.costs[target] = value; result.choices[target] = result.choices[source];
            }
        };
        for (int y = 0; y < height; y++) {
            for (int x = 1; x < width; x++) relax(y * width + x, y * width + x - 1, xs[x] - xs[x - 1]);
            for (int x = width - 2; x >= 0; x--) relax(y * width + x, y * width + x + 1, xs[x + 1] - xs[x]);
        }
        for (int x = 0; x < width; x++) {
            for (int y = 1; y < height; y++) relax(y * width + x, (y - 1) * width + x, ys[y] - ys[y - 1]);
            for (int y = height - 2; y >= 0; y--) relax(y * width + x, (y + 1) * width + x, ys[y + 1] - ys[y]);
        }
        return result;
    }
};

struct AxisResult { DBU cost; std::vector<DBU> positions; std::size_t states = 0; };

// 当全部 HBT 固定为同一个候选站点时，普通 routing 域是完整笛卡尔积，x/y 可以独立精确优化。
AxisResult solveAxis(const AssignedTopology& topology, const RootedTree& tree, const std::vector<bool>& isHbt,
                     const std::vector<DBU>& routingAxis, int axis, DBU common, bool traceback) {
    const auto& nodes = topology.tree.getNodes();
    std::vector<std::vector<DBU>> singletons(nodes.size()), costs(nodes.size());
    std::vector<const std::vector<DBU>*> domains(nodes.size(), &routingAxis);
    std::vector<std::vector<std::vector<int>>> choices(nodes.size());
    for (std::size_t i = 0; i < nodes.size(); i++) {
        if (!nodes[i].pins.empty() || isHbt[i]) {
            singletons[i] = {isHbt[i] ? common : nodes[i].position[axis]}; domains[i] = &singletons[i];
        }
    }
    AxisResult result; result.cost = 0;
    for (auto it = tree.preorder.rbegin(); it != tree.preorder.rend(); ++it) {
        int node = *it;
        const auto& targets = *domains[node];
        costs[node].assign(targets.size(), 0); result.states += targets.size();
        if (traceback) choices[node].resize(tree.children[node].size());
        for (std::size_t slot = 0; slot < tree.children[node].size(); slot++) {
            int child = tree.children[node][slot];
            const auto& seeds = *domains[child];
            std::vector<DBU> message(targets.size(), infinity);
            std::vector<int> selected(targets.size(), -1);
            auto update = [&](std::size_t i, DBU value, int choice) {
                if (value < message[i] || (value == message[i] && (selected[i] < 0 || choice < selected[i]))) {
                    message[i] = value; selected[i] = choice;
                }
            };
            DBU best = infinity; int bestIndex = -1; std::size_t j = 0;
            for (std::size_t i = 0; i < targets.size(); i++) {
                while (j < seeds.size() && seeds[j] <= targets[i]) {
                    DBU value = costs[child][j] - seeds[j];
                    if (bestIndex == -1 || value < best || (value == best && static_cast<int>(j) < bestIndex)) { best = value; bestIndex = j; }
                    j++;
                }
                if (bestIndex != -1) update(i, best + targets[i], bestIndex);
            }
            best = infinity; bestIndex = -1; int k = seeds.size() - 1;
            for (int i = targets.size() - 1; i >= 0; i--) {
                while (k >= 0 && seeds[k] >= targets[i]) {
                    DBU value = costs[child][k] + seeds[k];
                    if (bestIndex == -1 || value < best || (value == best && k < bestIndex)) { best = value; bestIndex = k; }
                    k--;
                }
                if (bestIndex != -1) update(i, best - targets[i], bestIndex);
            }
            for (std::size_t i = 0; i < targets.size(); i++) costs[node][i] = add(costs[node][i], message[i]);
            if (traceback) choices[node][slot] = std::move(selected);
        }
    }
    int rootChoice = std::min_element(costs[tree.root].begin(), costs[tree.root].end()) - costs[tree.root].begin();
    result.cost = costs[tree.root][rootChoice];
    if (traceback) {
        result.positions.resize(nodes.size());
        std::vector<int> selected(nodes.size(), 0); selected[tree.root] = rootChoice;
        for (int node : tree.preorder) {
            result.positions[node] = (*domains[node])[selected[node]];
            for (std::size_t i = 0; i < tree.children[node].size(); i++) selected[tree.children[node][i]] = choices[node][i][selected[node]];
        }
    }
    return result;
}
} // namespace

LocationResult HbtLocationOptimizer::runPaper(const AssignedTopology& topology, const HbtGrid& hbt,
                                             const HbtGrid& routing, std::uint32_t seed, bool coalesceTies) const {
    return runPaperImpl(topology, hbt, routing, seed, coalesceTies, false);
}

LocationResult HbtLocationOptimizer::runPaperFullDomainAudit(const AssignedTopology& topology, const HbtGrid& hbt,
                                                           const HbtGrid& routing, std::uint32_t seed) const {
    return runPaperImpl(topology, hbt, routing, seed, false, true);
}

LocationResult HbtLocationOptimizer::runPaperImpl(const AssignedTopology& topology, const HbtGrid& hbt,
    const HbtGrid& routing, std::uint32_t seed, bool coalesceTies, bool fullDomainAudit) const {
    const auto isHbt = hbtMask(topology);
    const auto& nodes = topology.tree.getNodes();
    LocationResult result; result.mode = "paper_region_eight_directions_dp";
    if (fullDomainAudit) result.mode = "diagnostic_full_domain_same_paper_forest";
    result.paperTiePolicy = coalesceTies ? "coalesce" : "first";
    for (const auto& node : nodes) result.positions.push_back(node.position);
    if (nodes.empty()) return result;
    std::unique_ptr<L1Grid> auditGrid;
    if (fullDomainAudit) auditGrid.reset(new L1Grid(hbt, routing, topology.tree));
    std::vector<std::vector<int>> adjacent(nodes.size()), children(nodes.size());
    for (const auto& edge : topology.tree.getEdges()) { adjacent[edge.u].push_back(edge.v); adjacent[edge.v].push_back(edge.u); }
    std::vector<int> starts;
    for (std::size_t i = 0; i < nodes.size(); i++) if (nodes[i].pins.empty()) starts.push_back(i);
    std::mt19937 random(seed);
    std::shuffle(starts.begin(), starts.end(), random);
    std::vector<bool> visited(nodes.size(), false);
    for (int start : starts) {
        if (visited[start]) continue;
        result.paperRoots.push_back(start); visited[start] = true;
        std::queue<int> queue; queue.push(start);
        while (!queue.empty()) {
            int node = queue.front(); queue.pop(); result.paperBfsOrder.push_back(node);
            for (int next : adjacent[node]) {
                // 固定引脚终止本分支；同一固定点可成为不同优化树的边界叶子。
                if (!nodes[next].pins.empty()) children[node].push_back(next);
                else if (!visited[next]) { visited[next] = true; children[node].push_back(next); queue.push(next); }
            }
        }
    }
    std::vector<utils::BoxT<DBU>> regions(nodes.size());
    std::vector<std::vector<utils::PointT<DBU>>> points(nodes.size());
    std::vector<std::vector<DBU>> costs(nodes.size());
    std::vector<std::vector<std::vector<int>>> choices(nodes.size());
    std::vector<std::vector<int>> sites(nodes.size());
    for (std::size_t i = 0; i < nodes.size(); i++) if (!nodes[i].pins.empty()) {
        regions[i].Set(nodes[i].position); points[i] = {nodes[i].position}; costs[i] = {0};
    }
    for (auto it = result.paperBfsOrder.rbegin(); it != result.paperBfsOrder.rend(); ++it) {
        int node = *it;
        if (children[node].empty()) regions[node] = routing.bounds();
        else for (int axis = 0; axis < 2; axis++) {
            std::vector<DBU> ends;
            for (int child : children[node]) { ends.push_back(regions[child][axis].low); ends.push_back(regions[child][axis].high); }
            std::sort(ends.begin(), ends.end());
            regions[node][axis] = {ends[ends.size() / 2 - 1], ends[ends.size() / 2]};
        }
        const auto& domain = isHbt[node] ? hbt : routing;
        sites[node] = domain.within(fullDomainAudit ? domain.bounds() : regions[node]);
        if (!fullDomainAudit) for (int dx = -1; dx <= 1; dx++) for (int dy = -1; dy <= 1; dy++) {
            if (dx == 0 && dy == 0) continue;
            auto extra = domain.nearestAll(regions[node], dx, dy);
            sites[node].insert(sites[node].end(), extra.begin(), extra.end());
        }
        std::sort(sites[node].begin(), sites[node].end());
        sites[node].erase(std::unique(sites[node].begin(), sites[node].end()), sites[node].end());
        if (sites[node].empty()) {
            if (isHbt[node] && hbt.available() == 0) throw HbtResourceExhausted();
            throw std::logic_error("Empty location candidate union although its domain is nonempty, or the routing domain is exhausted");
        }
        for (int site : sites[node]) points[node].push_back(domain.point(site));
        result.candidateCount += points[node].size();
        if (fullDomainAudit) {
            costs[node].assign(points[node].size(), 0);
            choices[node].assign(points[node].size(), std::vector<int>(children[node].size(), -1));
            for (std::size_t slot = 0; slot < children[node].size(); slot++) {
                int child = children[node][slot];
                if (points[child].size() == 1) {
                    for (std::size_t i = 0; i < points[node].size(); i++) {
                        costs[node][i] = add(costs[node][i], add(costs[child][0], utils::Dist(points[node][i], points[child][0])));
                        choices[node][i][slot] = 0;
                    }
                } else {
                    Domain source; source.points = points[child]; auditGrid->indexDomain(source);
                    auto message = auditGrid->transform(source, costs[child]);
                    for (std::size_t i = 0; i < points[node].size(); i++) {
                        int index = auditGrid->index(points[node][i]);
                        costs[node][i] = add(costs[node][i], message.costs[index]);
                        choices[node][i][slot] = message.choices[index];
                    }
                }
            }
            continue;
        }
        for (auto point : points[node]) {
            DBU value = 0;
            std::vector<int> selected;
            for (int child : children[node]) {
                DBU best = infinity; int index = -1;
                for (std::size_t j = 0; j < points[child].size(); j++) {
                    DBU cost = add(costs[child][j], utils::Dist(point, points[child][j]));
                    if (cost < best) { best = cost; index = j; }
                }
                value = add(value, best); selected.push_back(index);
            }
            costs[node].push_back(value); choices[node].push_back(std::move(selected));
        }
    }
    std::vector<int> selected(nodes.size(), 0);
    result.continuousPositions = result.positions;
    for (int node : result.paperRoots) {
        selected[node] = std::min_element(costs[node].begin(), costs[node].end()) - costs[node].begin();
        result.continuousPositions[node] = {regions[node].cx(), regions[node].cy()};
    }
    for (int node : result.paperBfsOrder) {
        result.positions[node] = points[node][selected[node]];
        for (std::size_t i = 0; i < children[node].size(); i++) {
            int child = children[node][i];
            if (!nodes[child].pins.empty()) continue;
            selected[child] = choices[node][selected[node]][i];
            for (int axis = 0; axis < 2; axis++) result.continuousPositions[child][axis] =
                std::max(regions[child][axis].low, std::min(regions[child][axis].high, result.continuousPositions[node][axis]));
        }
    }
    auto finishPaper = [&]() {
        std::vector<int> chosen;
        for (int node : topology.hbtNodes) chosen.push_back(sites[node][selected[node]]);
        finish(result, topology, hbt, &chosen);
    };
    finishPaper();
    result.paperWirelengthBeforeTieBreak = result.wirelength;
    result.paperSitesBeforeTieBreak = std::set<int>(result.hbtSites.begin(), result.hbtSites.end()).size();
    if (coalesceTies && result.paperSitesBeforeTieBreak > 1) {
        // 只在原 Algorithm-1 候选域和原公式 (7) 最优值内选择等价解。
        // 枚举 HBT 候选交集，检查共点是否不增加线长；不预留后续网站点，
        // 不扩大候选集，也不在原最优解之外强制合并。
        auto commonCandidates = [&](const std::vector<int>& group) {
            std::vector<int> common = sites[group.front()];
            for (int node : group) {
                std::vector<int> intersection;
                std::set_intersection(common.begin(), common.end(), sites[node].begin(), sites[node].end(), std::back_inserter(intersection));
                common.swap(intersection);
            }
            return common;
        };
        auto tryMerge = [&](const std::vector<int>& group, int site) {
            std::vector<bool> forced(nodes.size(), false);
            for (int node : group) forced[node] = true;
            auto restrictedCosts = costs;
            auto restrictedChoices = choices;
            for (auto it = result.paperBfsOrder.rbegin(); it != result.paperBfsOrder.rend(); ++it) {
                int node = *it;
                std::fill(restrictedCosts[node].begin(), restrictedCosts[node].end(), infinity);
                for (std::size_t i = 0; i < points[node].size(); i++) {
                    if (forced[node] && sites[node][i] != site) continue;
                    DBU value = 0;
                    for (std::size_t slot = 0; slot < children[node].size(); slot++) {
                        int child = children[node][slot];
                        DBU best = infinity; int index = -1;
                        for (std::size_t j = 0; j < points[child].size(); j++) {
                            DBU candidate = add(restrictedCosts[child][j], utils::Dist(points[node][i], points[child][j]));
                            if (candidate < best) { best = candidate; index = j; }
                        }
                        value = add(value, best);
                        restrictedChoices[node][i][slot] = index;
                    }
                    restrictedCosts[node][i] = value;
                }
            }
            bool equal = true;
            for (int root : result.paperRoots) {
                if (*std::min_element(restrictedCosts[root].begin(), restrictedCosts[root].end()) !=
                    *std::min_element(costs[root].begin(), costs[root].end())) { equal = false; break; }
            }
            if (!equal) return false;
            auto trialSelected = selected;
            for (int root : result.paperRoots) trialSelected[root] = std::min_element(restrictedCosts[root].begin(), restrictedCosts[root].end()) - restrictedCosts[root].begin();
            for (int node : result.paperBfsOrder) {
                if (restrictedCosts[node][trialSelected[node]] != costs[node][trialSelected[node]]) throw std::logic_error("Coalescing changed an optimal subtree cost");
                for (std::size_t slot = 0; slot < children[node].size(); slot++) {
                    int child = children[node][slot];
                    if (nodes[child].pins.empty()) trialSelected[child] = restrictedChoices[node][trialSelected[node]][slot];
                }
            }
            std::set<int> trialSites;
            for (int node : topology.hbtNodes) trialSites.insert(sites[node][trialSelected[node]]);
            if (trialSites.size() >= std::set<int>(result.hbtSites.begin(), result.hbtSites.end()).size()) return false;
            selected.swap(trialSelected);
            PaperTieMerge merge; merge.forcedNodes = group; merge.site = site;
            for (int node : result.paperBfsOrder) {
                merge.selected.push_back(selected[node]);
                result.positions[node] = points[node][selected[node]];
                // 仅替换实际回溯路径上的等成本前驱；完整原成本表保持不变。
                choices[node][selected[node]] = restrictedChoices[node][selected[node]];
            }
            result.paperTieMerges.push_back(std::move(merge));
            finishPaper();
            if (result.wirelength != result.paperWirelengthBeforeTieBreak || trialSites.size() != std::set<int>(result.hbtSites.begin(), result.hbtSites.end()).size()) {
                throw std::logic_error("Coalescing tie-break violated the original placement objective");
            }
            return true;
        };
        for (int site : commonCandidates(topology.hbtNodes)) {
            result.paperTieTrials++;
            if (tryMerge(topology.hbtNodes, site)) break;
        }
        // 全部 HBT 无法零代价共点时，继续尝试合并两个现有站点组。
        // 每次严格减少站点数；256 仅是计算量上限，不是资源预算。
        constexpr std::size_t pairTrialLimit = 256;
        bool improved = true;
        while (improved && result.paperPairTieTrials < pairTrialLimit) {
            std::map<int, std::vector<int>> groups;
            for (std::size_t i = 0; i < topology.hbtNodes.size(); i++) groups[result.hbtSites[i]].push_back(topology.hbtNodes[i]);
            if (groups.size() <= 2) break;
            improved = false;
            for (auto first = groups.begin(); first != groups.end() && !improved && result.paperPairTieTrials < pairTrialLimit; ++first) {
                for (auto second = std::next(first); second != groups.end() && !improved && result.paperPairTieTrials < pairTrialLimit; ++second) {
                    auto group = first->second;
                    group.insert(group.end(), second->second.begin(), second->second.end());
                    for (int site : commonCandidates(group)) {
                        if (result.paperPairTieTrials == pairTrialLimit) break;
                        result.paperPairTieTrials++;
                        if (tryMerge(group, site)) { improved = true; break; }
                    }
                }
            }
        }
    }
    for (int node : result.paperBfsOrder) {
        PaperNodeAudit audit;
        audit.node = node; audit.hbt = isHbt[node]; audit.children = children[node]; audit.region = regions[node];
        audit.candidateSites = sites[node]; audit.costs = costs[node]; audit.childChoices = choices[node]; audit.selected = selected[node];
        result.paperNodes.push_back(std::move(audit));
    }
    for (const auto& edge : topology.tree.getEdges()) result.continuousWirelength = add(result.continuousWirelength,
        utils::Dist(result.continuousPositions[edge.u], result.continuousPositions[edge.v]));
    finishPaper();
    return result;
}

PaperAssignmentSolution HbtLocationOptimizer::runPaperAssignments(const H3DTree& tree, const DieAssignmentResult& initial,
    const HbtGrid& hbt, const HbtGrid& routing, std::uint32_t seed, bool coalesceTies, int trialLimit) const {
    if (trialLimit < 1 || trialLimit > 1024) throw std::invalid_argument("Paper assignment trials must be in [1,1024]");
    PaperAssignmentSolution result;
    result.assignment = initial;
    result.expanded = expandAssignment(tree, initial);
    result.location = runPaper(result.expanded, hbt, routing, seed, coalesceTies);
    auto physicalSites = [](const LocationResult& value) { return static_cast<int>(std::set<int>(value.hbtSites.begin(), value.hbtSites.end()).size()); };
    auto appendTrial = [&](const DieAssignmentResult& assignment, const LocationResult& location, std::uint32_t tieSeed) {
        result.trials.push_back({tieSeed, assignment.edgeDies, assignment.hbtNodes, physicalSites(location), location.wirelength});
    };
    appendTrial(initial, result.location, 0);
    std::set<std::vector<int>> seen;
    auto signature = initial.hbtNodes; std::sort(signature.begin(), signature.end()); seen.insert(signature);
    std::mt19937 random(seed);
    for (int trial = 1; trial < trialLimit && physicalSites(result.location) > 1; trial++) {
        auto tieSeed = static_cast<std::uint32_t>(random());
        if (!tieSeed) tieSeed = 1;
        auto assignment = DieAssignment().run(tree, initial.root, Die::Bottom, tieSeed);
        if (assignment.hbtCount != initial.hbtCount) throw std::logic_error("Quantity tie search changed the mhc optimum");
        signature = assignment.hbtNodes; std::sort(signature.begin(), signature.end());
        if (!seen.insert(signature).second) continue;
        auto expanded = expandAssignment(tree, assignment);
        auto location = runPaper(expanded, hbt, routing, seed, coalesceTies);
        appendTrial(assignment, location, tieSeed);
        if (physicalSites(location) < physicalSites(result.location) ||
            (physicalSites(location) == physicalSites(result.location) && location.wirelength < result.location.wirelength)) {
            result.selectedTrial = result.trials.size() - 1;
            result.assignment = std::move(assignment); result.expanded = std::move(expanded); result.location = std::move(location);
        }
    }
    return result;
}

LocationResult HbtLocationOptimizer::run(const AssignedTopology& topology, const HbtGrid& hbt,
                                        const HbtGrid& routing) const {
    const auto isHbt = hbtMask(topology);
    const auto& nodes = topology.tree.getNodes();
    LocationResult result;
    for (const auto& node : nodes) result.positions.push_back(node.position);
    if (nodes.empty()) return result;
    L1Grid grid(hbt, routing, topology.tree);
    Domain hbtDomain, routingDomain;
    for (int site : hbt.within(hbt.bounds())) hbtDomain.points.push_back(hbt.point(site));
    for (int site : routing.within(routing.bounds())) routingDomain.points.push_back(routing.point(site));
    grid.indexDomain(hbtDomain); grid.indexDomain(routingDomain);
    std::vector<Domain> fixed(nodes.size());
    std::vector<const Domain*> domains(nodes.size());
    std::vector<std::vector<int>> adjacent(nodes.size()), children(nodes.size());
    std::vector<std::vector<DBU>> costs(nodes.size());
    std::vector<std::vector<std::vector<int>>> choices(nodes.size());
    for (std::size_t i = 0; i < nodes.size(); i++) {
        if (!nodes[i].pins.empty()) {
            fixed[i].points = {nodes[i].position}; grid.indexDomain(fixed[i]); domains[i] = &fixed[i]; costs[i] = {0};
        } else {
            domains[i] = isHbt[i] ? &hbtDomain : &routingDomain;
            if (domains[i]->points.empty()) throw std::runtime_error("No available HBT/location candidate");
        }
    }
    for (const auto& edge : topology.tree.getEdges()) { adjacent[edge.u].push_back(edge.v); adjacent[edge.v].push_back(edge.u); }
    std::vector<int> order, roots;
    std::vector<bool> visited(nodes.size(), false);
    for (std::size_t start = 0; start < nodes.size(); start++) {
        if (visited[start] || !nodes[start].pins.empty()) continue;
        roots.push_back(start); visited[start] = true;
        std::queue<int> queue; queue.push(start);
        while (!queue.empty()) {
            int node = queue.front(); queue.pop(); order.push_back(node);
            for (int next : adjacent[node]) {
                if (!nodes[next].pins.empty()) children[node].push_back(next);
                else if (!visited[next]) { visited[next] = true; children[node].push_back(next); queue.push(next); }
            }
        }
    }
    for (auto it = order.rbegin(); it != order.rend(); ++it) {
        int node = *it;
        const auto& domain = *domains[node];
        costs[node].assign(domain.points.size(), 0); choices[node].resize(children[node].size());
        result.candidateCount += domain.points.size();
        for (std::size_t slot = 0; slot < children[node].size(); slot++) {
            int child = children[node][slot];
            auto& selected = choices[node][slot]; selected.resize(domain.points.size());
            if (domains[child]->points.size() == 1) {
                for (std::size_t i = 0; i < domain.points.size(); i++) {
                    costs[node][i] = add(costs[node][i], add(costs[child][0], utils::Dist(domain.points[i], domains[child]->points[0])));
                    selected[i] = 0;
                }
            } else {
                auto message = grid.transform(*domains[child], costs[child]);
                for (std::size_t i = 0; i < domain.points.size(); i++) {
                    costs[node][i] = add(costs[node][i], message.costs[domain.indices[i]]);
                    selected[i] = message.choices[domain.indices[i]];
                }
            }
        }
    }
    std::vector<int> selected(nodes.size(), 0);
    for (int root : roots) selected[root] = std::min_element(costs[root].begin(), costs[root].end()) - costs[root].begin();
    for (int node : order) {
        result.positions[node] = domains[node]->points[selected[node]];
        for (std::size_t i = 0; i < children[node].size(); i++) {
            int child = children[node][i];
            if (nodes[child].pins.empty()) selected[child] = choices[node][i][selected[node]];
        }
    }
    finish(result, topology, hbt);
    return result;
}

LocationResult HbtLocationOptimizer::runSingleSite(const AssignedTopology& topology, const HbtGrid& hbt,
                                                  const HbtGrid& routing) const {
    const auto isHbt = hbtMask(topology);
    if (topology.hbtNodes.empty()) return run(topology, hbt, routing);
    auto free = hbt.within(hbt.bounds());
    if (free.empty()) throw std::runtime_error("No available HBT/location candidate");
    LocationResult result; result.mode = "exact_single_site_l1";
    DBU best = infinity; int bestSite = -1;
    if (routing.available() == routing.size()) {
        const auto tree = topology.tree.rootAt();
        std::array<std::map<DBU, DBU>, 2> axisCosts;
        for (int axis = 0; axis < 2; axis++) {
            std::set<DBU> coordinates;
            for (int site : free) coordinates.insert(hbt.point(site)[axis]);
            const auto& values = axis == 0 ? routing.xCoordinates() : routing.yCoordinates();
            for (DBU coordinate : coordinates) {
                auto solved = solveAxis(topology, tree, isHbt, values, axis, coordinate, false);
                axisCosts[axis][coordinate] = solved.cost; result.candidateCount += solved.states;
            }
        }
        for (int site : free) {
            auto point = hbt.point(site);
            DBU value = add(axisCosts[0].at(point.x), axisCosts[1].at(point.y));
            if (value < best) { best = value; bestSite = site; }
        }
        auto point = hbt.point(bestSite);
        auto x = solveAxis(topology, tree, isHbt, routing.xCoordinates(), 0, point.x, true);
        auto y = solveAxis(topology, tree, isHbt, routing.yCoordinates(), 1, point.y, true);
        result.candidateCount += x.states + y.states;
        for (std::size_t i = 0; i < x.positions.size(); i++) result.positions.emplace_back(x.positions[i], y.positions[i]);
        finish(result, topology, hbt);
        if (result.wirelength != best) throw std::logic_error("Single-site axis traceback cost mismatch");
    } else {
        // 有洞的普通 routing 域不能分离 x/y；逐个站点调用完整二维精确解作为保守后备。
        std::size_t states = 0;
        for (int site : free) {
            auto point = hbt.point(site);
            HbtGrid single({point.x}, {point.y});
            auto candidate = run(topology, single, routing); states += candidate.candidateCount;
            if (candidate.wirelength < best) { best = candidate.wirelength; bestSite = site; result = std::move(candidate); }
        }
        result.hbtSites.assign(topology.hbtNodes.size(), bestSite);
        result.candidateCount = states; result.mode = "exact_single_site_l1";
    }
    return result;
}

LocationResult HbtLocationOptimizer::runWithSiteBudget(const AssignedTopology& topology, const HbtGrid& hbt,
                                                      const HbtGrid& routing, int maxSites) const {
    if (topology.hbtNodes.empty()) return run(topology, hbt, routing);
    if (maxSites < 1) throw std::runtime_error("HBT site budget cannot cover the current cross-die net");
    LocationResult result;
    if (maxSites == 1) {
        result = runSingleSite(topology, hbt, routing); result.budgetFallback = true;
    } else {
        result = run(topology, hbt, routing);
        if (std::set<int>(result.hbtSites.begin(), result.hbtSites.end()).size() > static_cast<std::size_t>(maxSites)) {
            auto states = result.candidateCount;
            result = runSingleSite(topology, hbt, routing); result.candidateCount += states; result.budgetFallback = true;
        }
    }
    result.siteBudget = maxSites;
    return result;
}

} // namespace h3d
