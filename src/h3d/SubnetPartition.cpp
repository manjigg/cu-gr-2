#include "SubnetPartition.h"
#include <map>
#include <numeric>
#include <set>
#include <stdexcept>

namespace h3d {
namespace {
class Components {
    std::vector<int> parents, sizes;
public:
    explicit Components(int size): parents(size), sizes(size, 1) { std::iota(parents.begin(), parents.end(), 0); }
    int root(int node) {
        int result = node;
        while (parents[result] != result) result = parents[result];
        while (parents[node] != node) { int next = parents[node]; parents[node] = result; node = next; }
        return result;
    }
    void connect(int u, int v) {
        u = root(u); v = root(v);
        if (u == v) return;
        if (sizes[u] < sizes[v]) std::swap(u, v);
        parents[v] = u; sizes[u] += sizes[v];
    }
};
}

SubnetPartition partitionAtHbts(const AssignedTopology& topology, const LocationResult& locations) {
    SubnetPartition result;
    auto tree = topology.tree.rootAt(topology.root);
    result.root = tree.root;
    result.reverseDfsOrder.assign(tree.preorder.rbegin(), tree.preorder.rend());
    const int count = topology.tree.getNodes().size();
    if (count > std::numeric_limits<int>::max() / 2 || locations.positions.size() != static_cast<std::size_t>(count) ||
        topology.edgeDies.size() != topology.tree.getEdges().size() || locations.hbtSites.size() != topology.hbtNodes.size()) {
        throw std::invalid_argument("Invalid HBT split input");
    }
    Components sets(2 * count);
    std::vector<int> masks(count, 0), siteAtNode(count, -1);
    std::map<int, int> aliases;
    for (std::size_t i = 0; i < topology.hbtNodes.size(); i++) {
        int node = topology.hbtNodes[i], site = locations.hbtSites[i];
        if (node < 0 || node >= count || site < 0 || siteAtNode[node] != -1) throw std::invalid_argument("Invalid HBT split terminal");
        siteAtNode[node] = site; masks[node] = 3;
        auto inserted = aliases.emplace(site, node);
        if (!inserted.second) {
            if (locations.positions[node] != locations.positions[inserted.first->second]) throw std::invalid_argument("Merged HBT site has inconsistent positions");
            for (int die = 0; die < 2; die++) sets.connect(die * count + node, die * count + inserted.first->second);
        }
    }
    for (int node : result.reverseDfsOrder) {
        for (const auto& pin : topology.tree.getNodes()[node].pins) masks[node] |= 1 << static_cast<int>(pin.die);
        if (tree.parentEdges[node] == -1) continue;
        int die = static_cast<int>(topology.edgeDies[tree.parentEdges[node]]);
        if (die < 0 || die >= 2) throw std::invalid_argument("Invalid die on a split edge");
        int parent = tree.parents[node];
        masks[node] |= 1 << die; masks[parent] |= 1 << die;
        sets.connect(die * count + node, die * count + parent);
    }
    for (int node = 0; node < count; node++) if (masks[node] == 3 && siteAtNode[node] == -1) {
        throw std::invalid_argument("Cross-die junction has no HBT");
    }
    std::map<std::pair<int, int>, int> indices;
    for (int node : result.reverseDfsOrder) for (int die = 0; die < 2; die++) if (masks[node] & (1 << die)) {
        auto key = std::make_pair(die, sets.root(die * count + node));
        auto inserted = indices.emplace(key, result.components.size());
        if (inserted.second) { DieComponent part; part.die = static_cast<Die>(die); result.components.push_back(part); }
        auto& part = result.components[inserted.first->second];
        part.nodes.push_back(node);
        if (siteAtNode[node] != -1) part.hbtSites.push_back(siteAtNode[node]);
    }
    for (int node : result.reverseDfsOrder) {
        int edge = tree.parentEdges[node];
        if (edge == -1) continue;
        int die = static_cast<int>(topology.edgeDies[edge]);
        result.components.at(indices.at({die, sets.root(die * count + node)})).edges.push_back(edge);
    }
    for (auto& part : result.components) {
        std::sort(part.hbtSites.begin(), part.hbtSites.end());
        part.hbtSites.erase(std::unique(part.hbtSites.begin(), part.hbtSites.end()), part.hbtSites.end());
    }
    return result;
}

} // namespace h3d
