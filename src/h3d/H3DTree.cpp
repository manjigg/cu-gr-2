#include "H3DTree.h"

#include <limits>
#include <stdexcept>
#include <utility>

namespace h3d {

int chooseLeafRoot(const H3DTree& tree, std::mt19937& random) {
    if (tree.getNodes().empty()) throw std::invalid_argument("Cannot choose a root for an empty tree");
    if (tree.getNodes().size() == 1) return 0;
    std::vector<int> degrees(tree.getNodes().size(), 0), leaves;
    for (const auto& edge : tree.getEdges()) { degrees[edge.u]++; degrees[edge.v]++; }
    for (std::size_t i = 0; i < degrees.size(); i++) if (degrees[i] == 1) leaves.push_back(i);
    if (leaves.empty()) throw std::invalid_argument("No degree-one root for paper die assignment");
    std::uniform_int_distribution<std::size_t> choose(0, leaves.size() - 1);
    return leaves[choose(random)];
}

int H3DTree::addNode(utils::PointT<DBU> position, std::vector<PinReference> pins) {
    if (nodes.size() >= static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        throw std::length_error("Too many H3D topology nodes");
    }
    for (const auto& pin : pins) {
        if (pin.die != Die::Bottom && pin.die != Die::Top) {
            throw std::invalid_argument("An H3D pin must belong to the bottom or top die");
        }
    }
    int index = static_cast<int>(nodes.size());
    nodes.push_back({position, std::move(pins)});
    return index;
}

int H3DTree::addEdge(int u, int v) {
    if (u < 0 || v < 0 || static_cast<std::size_t>(u) >= nodes.size() ||
        static_cast<std::size_t>(v) >= nodes.size()) {
        throw std::out_of_range("Invalid H3D topology edge endpoint");
    }
    if (u == v) throw std::invalid_argument("An H3D topology edge cannot be a self-loop");
    if (edges.size() >= static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        throw std::length_error("Too many H3D topology edges");
    }
    int index = static_cast<int>(edges.size());
    edges.push_back({u, v});
    return index;
}

RootedTree H3DTree::rootAt(int root) const {
    RootedTree tree;
    if (root < -1 || (root >= 0 && static_cast<std::size_t>(root) >= nodes.size())) {
        throw std::out_of_range("Invalid H3D topology root");
    }
    if (nodes.empty()) return tree;
    if (edges.size() != nodes.size() - 1) {
        throw std::invalid_argument("An H3D topology must have |V|-1 edges");
    }

    // 邻接项为 (相邻节点编号, 原始边编号)，保证重新选根不会改变边的身份。
    std::vector<std::vector<std::pair<int, int>>> adjacent(nodes.size());
    for (std::size_t i = 0; i < edges.size(); i++) {
        const auto& edge = edges[i];
        adjacent[edge.u].emplace_back(edge.v, static_cast<int>(i));
        adjacent[edge.v].emplace_back(edge.u, static_cast<int>(i));
    }
    if (root == -1) {
        root = 0;
        for (std::size_t i = 0; i < nodes.size(); i++) {
            if (adjacent[i].size() == 1) {
                root = static_cast<int>(i);
                break;
            }
        }
    }

    tree.root = root;
    tree.parents.assign(nodes.size(), -1);
    tree.parentEdges.assign(nodes.size(), -1);
    tree.children.resize(nodes.size());
    tree.preorder.reserve(nodes.size());
    std::vector<bool> visited(nodes.size(), false);
    std::vector<int> stack = {root};
    visited[root] = true;
    while (!stack.empty()) {
        int node = stack.back();
        stack.pop_back();
        tree.preorder.push_back(node);
        for (const auto& neighbor : adjacent[node]) {
            if (neighbor.second == tree.parentEdges[node]) continue;
            int child = neighbor.first;
            if (visited[child]) throw std::invalid_argument("An H3D topology contains a cycle");
            visited[child] = true;
            tree.parents[child] = node;
            tree.parentEdges[child] = neighbor.second;
            tree.children[node].push_back(child);
            stack.push_back(child);
        }
    }
    if (tree.preorder.size() != nodes.size()) {
        throw std::invalid_argument("An H3D topology must be connected");
    }
    return tree;
}

} // namespace h3d
