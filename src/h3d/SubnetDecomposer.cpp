#include "SubnetDecomposer.h"
#include <map>
#include <queue>
#include <set>
#include <stdexcept>
#include <numeric>

namespace h3d {
namespace {
struct Vertex {
    utils::PointT<DBU> position;
    std::vector<PinReference> pins;
    std::set<int> sites;
    std::set<int> adjacent;
};
using PinKey = std::tuple<int, int, int>;

::PinReference terminalPort(H3DDesign& design, int die, int site) {
    auto found = design.hbtPorts[die].find(site);
    if (found != design.hbtPorts[die].end()) return found->second;
    std::string name = "__h3d_hbt_" + std::to_string(site);
    while (!design.reservedPortNames[die].insert(name).second) name = "_" + name;
    auto point = design.physicalHbtPoint(site);
    auto half = design.padSize / static_cast<DBU>(2);
    BoxOnLayer box(design.landingLayers[die], point.x - half.x, point.y - half.y,
                   point.x + design.padSize.x - half.x, point.y + design.padSize.y - half.y);
    auto pin = design.dies[die].design->addPort(name, box);
    design.hbtPorts[die].emplace(site, pin);
    design.hbtPortNames[die].emplace(site, name);
    return pin;
}

std::shared_ptr<SteinerTreeNode> routingTree(const std::vector<Vertex>& vertices,
                                            const std::vector<int>& component,
                                            const Subnet& subnet, const GridGraph& graph) {
    // DBU 优化节点映射到 routing 网格。重合格子合并，保留全部固定层约束。
    std::map<std::pair<int, int>, int> indices;
    std::vector<std::shared_ptr<SteinerTreeNode>> nodes;
    std::vector<std::set<int>> adjacent;
    auto at = [&](utils::PointT<int> point) {
        auto key = std::make_pair(point.x, point.y);
        auto found = indices.find(key);
        if (found != indices.end()) return found->second;
        int index = nodes.size();
        indices[key] = index;
        nodes.push_back(std::make_shared<SteinerTreeNode>(point)); adjacent.emplace_back();
        return index;
    };
    for (int vertex : component) at(cellAt(graph, vertices[vertex].position));
    for (int vertex : component) {
        int u = at(cellAt(graph, vertices[vertex].position));
        for (int next : vertices[vertex].adjacent) {
            int v = at(cellAt(graph, vertices[next].position));
            if (u != v) { adjacent[u].insert(v); adjacent[v].insert(u); }
        }
    }
    for (const auto& terminal : subnet.terminals) {
        int index = at(terminal);
        nodes[index]->fixedLayers.Update(terminal.layerIdx);
        nodes[index]->fixedLayers.Update(std::min(terminal.layerIdx + 2, static_cast<int>(graph.getNumLayers()) - 1));
    }
    int root = 0;
    for (std::size_t i = 0; i < nodes.size(); i++) if (adjacent[i].size() <= 1) { root = i; break; }
    std::queue<int> queue; queue.push(root);
    std::vector<bool> visited(nodes.size(), false); visited[root] = true;
    int count = 0;
    while (!queue.empty()) {
        int node = queue.front(); queue.pop(); count++;
        for (int next : adjacent[node]) if (!visited[next]) {
            visited[next] = true; nodes[node]->children.push_back(nodes[next]); queue.push(next);
        }
    }
    if (count != nodes.size()) throw std::runtime_error("Disconnected decomposed routing topology");
    return nodes[root];
}
} // namespace

std::vector<Subnet> SubnetDecomposer::run(const OriginalNet& net, const AssignedTopology& topology,
                                         const LocationResult& locations, H3DDesign& design) const {
    if (locations.positions.size() != topology.tree.getNodes().size() ||
        locations.hbtSites.size() != topology.hbtNodes.size() ||
        topology.edgeDies.size() != topology.tree.getEdges().size()) {
        throw std::invalid_argument("Inconsistent HBT location/decomposition input");
    }
    std::map<PinKey, GRPoint> access;
    for (std::size_t i = 0; i < net.pins.size(); i++) {
        const auto& pin = net.pins[i];
        access.emplace(PinKey(static_cast<int>(pin.die), pin.instanceIndex, pin.pinIndex), net.access[i]);
    }
    std::array<std::vector<Vertex>, 2> vertices;
    std::array<std::map<std::pair<DBU, DBU>, int>, 2> indices;
    auto at = [&](int die, int source) {
        auto point = locations.positions.at(source);
        auto key = std::make_pair(point.x, point.y);
        auto found = indices[die].find(key);
        if (found != indices[die].end()) return found->second;
        int index = vertices[die].size();
        indices[die][key] = index;
        Vertex vertex; vertex.position = point;
        vertices[die].push_back(vertex);
        return index;
    };
    for (std::size_t i = 0; i < topology.tree.getEdges().size(); i++) {
        const auto& edge = topology.tree.getEdges()[i];
        int die = static_cast<int>(topology.edgeDies[i]);
        int u = at(die, edge.u), v = at(die, edge.v);
        if (u != v) { vertices[die][u].adjacent.insert(v); vertices[die][v].adjacent.insert(u); }
    }
    for (std::size_t i = 0; i < topology.tree.getNodes().size(); i++) {
        for (const auto& pin : topology.tree.getNodes()[i].pins) {
            int die = static_cast<int>(pin.die);
            int vertex = at(die, i);
            vertices[die][vertex].pins.push_back(pin);
        }
    }
    for (std::size_t i = 0; i < topology.hbtNodes.size(); i++) for (int die = 0; die < 2; die++) {
        int vertex = at(die, topology.hbtNodes[i]);
        vertices[die][vertex].sites.insert(locations.hbtSites[i]);
    }
    std::vector<Subnet> result;
    for (int die = 0; die < 2; die++) {
        std::vector<bool> visited(vertices[die].size(), false);
        int componentId = 0;
        const auto& graph = design.dies[die].router->getGridGraph();
        for (std::size_t start = 0; start < vertices[die].size(); start++) {
            if (visited[start]) continue;
            std::vector<int> component, stack = {static_cast<int>(start)};
            visited[start] = true;
            Subnet subnet;
            subnet.original = net.id; subnet.die = die; subnet.use = net.use;
            subnet.name = "__h3d_n" + std::to_string(net.id) + "_d" + std::to_string(die) + "_s" + std::to_string(componentId++);
            while (!design.reservedNetNames[die].insert(subnet.name).second) subnet.name = "_" + subnet.name;
            while (!stack.empty()) {
                int vertex = stack.back(); stack.pop_back(); component.push_back(vertex);
                const auto& node = vertices[die][vertex];
                for (const auto& pin : node.pins) {
                    subnet.pins.emplace_back(pin.instanceIndex, pin.pinIndex);
                    subnet.terminals.push_back(access.at(PinKey(die, pin.instanceIndex, pin.pinIndex)));
                }
                for (int site : node.sites) {
                    subnet.pins.push_back(terminalPort(design, die, site));
                    subnet.hbtSites.push_back(site);
                    auto cell = cellAt(graph, design.hbt->point(site));
                    subnet.terminals.emplace_back(design.landingLayers[die], cell.x, cell.y);
                }
                for (int next : node.adjacent) if (!visited[next]) { visited[next] = true; stack.push_back(next); }
            }
            if (subnet.pins.empty()) throw std::runtime_error("A decomposed component has no terminals");
            subnet.topology = routingTree(vertices[die], component, subnet, graph);
            result.push_back(std::move(subnet));
        }
    }
    return result;
}

std::vector<Subnet> SubnetDecomposer::runPaper(const OriginalNet& net, const AssignedTopology& topology,
                                             const LocationResult& locations, const SubnetPartition& partition,
                                             H3DDesign& design) const {
    std::map<PinKey, GRPoint> access;
    for (std::size_t i = 0; i < net.pins.size(); i++) {
        const auto& pin = net.pins[i];
        access.emplace(PinKey(static_cast<int>(pin.die), pin.instanceIndex, pin.pinIndex), net.access[i]);
    }
    std::map<int, int> hbtSites;
    for (std::size_t i = 0; i < topology.hbtNodes.size(); i++) hbtSites[topology.hbtNodes[i]] = locations.hbtSites[i];
    std::array<int, 2> componentIds = {{0, 0}};
    std::vector<Subnet> result;
    for (const auto& part : partition.components) {
        int die = static_cast<int>(part.die);
        const auto& graph = design.dies[die].router->getGridGraph();
        std::vector<Vertex> vertices;
        std::map<std::pair<int, int>, int> indices;
        std::map<int, int> vertexAtNode;
        for (int source : part.nodes) {
            auto key = hbtSites.count(source) ? std::make_pair(1, hbtSites.at(source)) : std::make_pair(0, source);
            auto inserted = indices.emplace(key, vertices.size());
            if (inserted.second) { Vertex vertex; vertex.position = locations.positions.at(source); vertices.push_back(vertex); }
            vertexAtNode[source] = inserted.first->second;
        }
        for (int edge : part.edges) {
            const auto& connection = topology.tree.getEdges().at(edge);
            int u = vertexAtNode.at(connection.u), v = vertexAtNode.at(connection.v);
            if (u != v) { vertices[u].adjacent.insert(v); vertices[v].adjacent.insert(u); }
        }
        Subnet subnet; subnet.original = net.id; subnet.die = die; subnet.use = net.use;
        subnet.name = "__h3d_n" + std::to_string(net.id) + "_d" + std::to_string(die) + "_s" + std::to_string(componentIds[die]++);
        while (!design.reservedNetNames[die].insert(subnet.name).second) subnet.name = "_" + subnet.name;
        for (int source : part.nodes) for (const auto& pin : topology.tree.getNodes()[source].pins) if (static_cast<int>(pin.die) == die) {
            subnet.pins.emplace_back(pin.instanceIndex, pin.pinIndex);
            subnet.terminals.push_back(access.at(PinKey(die, pin.instanceIndex, pin.pinIndex)));
        }
        for (int site : part.hbtSites) {
            subnet.pins.push_back(terminalPort(design, die, site)); subnet.hbtSites.push_back(site);
            auto cell = cellAt(graph, design.hbt->point(site));
            subnet.terminals.emplace_back(design.landingLayers[die], cell.x, cell.y);
        }
        if (subnet.pins.empty()) throw std::runtime_error("Paper split component has no terminal");
        std::vector<int> component(vertices.size()); std::iota(component.begin(), component.end(), 0);
        if (design.config.value("subnet_routing_topology", std::string("inherited")) == "inherited")
            subnet.topology = routingTree(vertices, component, subnet, graph);
        result.push_back(std::move(subnet));
    }
    return result;
}

} // namespace h3d
