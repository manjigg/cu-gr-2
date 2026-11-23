#include <queue>
#include "MazeRoute.h"

void SparseGraph::init(GridGraphView<CostT>& wireCostView, SparseGrid& grid) {
    // 0. Create pseudo pins
    robin_hood::unordered_map<uint64_t, std::pair<utils::PointT<int>, utils::IntervalT<int>>> selectedAccessPoints;
    gridGraph.selectAccessPoints(net, selectedAccessPoints);
    pseudoPins.reserve(selectedAccessPoints.size());
    for (auto& selectedPoint : selectedAccessPoints) pseudoPins.push_back(selectedPoint.second);

    // 1. Collect additional routing grid lines
    vector<int> pxs;
    vector<int> pys;
    pxs.reserve(net.getNumPins());
    pys.reserve(net.getNumPins());
    for (const auto& pin : pseudoPins) {
        pxs.emplace_back(pin.first.x);
        pys.emplace_back(pin.first.y);
    }
    std::sort(pxs.begin(), pxs.end());
    std::sort(pys.begin(), pys.end());
    
    const int xSize = gridGraph.getSize(0);
    const int ySize = gridGraph.getSize(1);
    xs.reserve(xSize / grid.interval.x + pxs.size());
    ys.reserve(ySize / grid.interval.y + pys.size());
    int j = 0;
    for (int i = 0; true; i++) {
        int x = i * grid.interval.x + grid.offset.x;
        for ( ; j < pxs.size() && pxs[j] <= x; j++) {
            if ((xs.size() > 0 && pxs[j] == xs.back()) || pxs[j] == x) continue;
            xs.emplace_back(pxs[j]);
        }
        if (x < xSize) {
            xs.emplace_back(x);
        } else {
            break;
        }
    }
    j = 0;
    for (int i = 0; true; i++) {
        int y = i * grid.interval.y + grid.offset.y;
        for ( ; j < pys.size() && pys[j] <= y; j++) {
            if ((ys.size() > 0 && pys[j] == ys.back()) || pys[j] == y) continue;
            ys.emplace_back(pys[j]);
        }
        if (y < ySize) {
            ys.emplace_back(y);
        } else {
            break;
        }
    }
    
    // 2. Add vertices
    vertices.reserve(2 * xs.size() * ys.size());
    for (unsigned direction = 0; direction < 2; direction++) {
        for (auto& y : ys) {
            for (auto& x : xs) {
                vertices.emplace_back(direction, x, y);
            }
        }
    }
    
    // 3. Add same-layer connections
    edges.resize(vertices.size(), {-1, -1, -1});
    costs.resize(vertices.size(), {-1, -1, -1});
    auto addSameLayerEdge = [&] (const unsigned direction, const int xi, const int yi) {
        const int u = getVertexIndex(direction, xi, yi);
        const int v = direction == MetalLayer::H ? u + 1 : u + xs.size();
        utils::PointT<int> U(xs[xi], ys[yi]);
        utils::PointT<int> V(xs[xi + 1 - direction], ys[yi + direction]);
        
        edges[u][0] = v;
        edges[v][1] = u;
        costs[u][0] = costs[v][1] = wireCostView.sum(U, V);
    };
    
    for (unsigned direction = 0; direction < 2; direction++) {
        if (direction == MetalLayer::H) {
            for (int yi = 0; yi < ys.size(); yi++) {
                for (int xi = 0; xi + 1 < xs.size(); xi++) {
                    addSameLayerEdge(direction, xi, yi);
                }
            }
        } else {
            for (int xi = 0; xi < xs.size(); xi++) {
                for (int yi = 0; yi + 1 < ys.size(); yi++) {
                    addSameLayerEdge(direction, xi, yi);
                }
            }
        }
    }
    
    // 4. Add diff-layer connections
    auto addDiffLayerEdge = [&] (const int xi, const int yi) {
        const int u = getVertexIndex(0, xi, yi);
        const int v = u + xs.size() * ys.size();
        
        edges[u][2] = v;
        edges[v][2] = u;
        costs[u][2] = costs[v][2] = gridGraph.getUnitViaCost();
    };
    
    for (int xi = 0; xi < xs.size(); xi++) {
        for (int yi = 0; yi < ys.size(); yi++) {
            addDiffLayerEdge(xi, yi);
        }
    }
    
    // 5. Add pseudo pin locations
    robin_hood::unordered_map<int, int> xtoxi;
    robin_hood::unordered_map<int, int> ytoyi;
    for (int xi = 0; xi < xs.size(); xi++) xtoxi.emplace(xs[xi], xi);
    for (int yi = 0; yi < ys.size(); yi++) ytoyi.emplace(ys[yi], yi);
    
    pinVertex.resize(pseudoPins.size(), -1);
    for (int pinIndex = 0; pinIndex < pseudoPins.size(); pinIndex++) {
        const auto& pin = pseudoPins[pinIndex];
        const int xi = xtoxi[pin.first.x];
        const int yi = ytoyi[pin.first.y];
        const int u = getVertexIndex(0, xi, yi);
        vertexPin.emplace(u, pinIndex);
        pinVertex[pinIndex] = u;
        // Set the cost of the diff-layer connection at u to be 0
        costs[u][2] = 0;
        costs[u + xs.size() * ys.size()][2] = 0;
    }
}

void MazeRoute::run() {
    vector<CostT> minCosts(graph.getNumVertices(), std::numeric_limits<CostT>::max());
    solutions.reserve(net.getNumPins());
    auto compareSolution = [&] (const std::shared_ptr<Solution>& lhs, const std::shared_ptr<Solution>& rhs) {
        return lhs->cost > rhs->cost;
    };
    std::priority_queue<
        std::shared_ptr<Solution>, vector<std::shared_ptr<Solution>>, decltype(compareSolution)
    > queue(compareSolution);
    auto updateSolution = [&] (const std::shared_ptr<Solution>& solution) {
        queue.push(solution);
        if (solution->cost < minCosts[solution->vertex]) minCosts[solution->vertex] = solution->cost;
    };
    
    vector<bool> visited(net.getNumPins(), false);
    const int startPinIndex = 0;
    visited[startPinIndex] = true;
    int numDetached = graph.getNumPseudoPins() - 1;
    updateSolution(std::make_shared<Solution>(0, graph.getPinVertex(startPinIndex), nullptr));
    
    while (numDetached > 0) {
        std::shared_ptr<Solution> foundSolution;
        int foundPinIndex;
        while(!queue.empty()) {
            auto solution = queue.top();
            queue.pop();
            foundPinIndex = graph.getVertexPin(solution->vertex);
            if (foundPinIndex != -1 && !visited[foundPinIndex]) {
                foundSolution = solution;
                break;
            }
            // Pruning
            if (solution->cost > minCosts[solution->vertex]) continue;
            for (int edgeIndex = 0; edgeIndex < 3; edgeIndex++) {
                int nextVertex = graph.getNextVertex(solution->vertex, edgeIndex);
                if (nextVertex == -1 || (solution->prev && nextVertex == solution->prev->vertex)) continue;
                CostT nextCost = solution->cost + graph.getEdgeCost(solution->vertex, edgeIndex);
                if (nextCost < minCosts[nextVertex]) 
                    updateSolution(std::make_shared<Solution>(nextCost, nextVertex, solution));
            }
        }
        
        solutions.emplace_back(foundSolution);
        visited[foundPinIndex] = true;
        numDetached -= 1; 
        
        // Update the cost of the vertices on the path
        std::shared_ptr<Solution> temp = foundSolution;
        while (temp && temp->cost != 0) {
            updateSolution(std::make_shared<Solution>(0, temp->vertex, temp->prev));
            temp = temp->prev;
        }
    }
    
    if (numDetached != 0) {
        log() << "Error: failed to connect all pins." << std::endl;
    }
}


std::shared_ptr<SteinerTreeNode> MazeRoute::getSteinerTree() const {
    std::shared_ptr<SteinerTreeNode> tree = nullptr;
    if (graph.getNumPseudoPins() == 1) {
        const auto& pseudoPin = graph.getPseudoPin(0);
        tree = std::make_shared<SteinerTreeNode>(pseudoPin.first, pseudoPin.second);
        return tree;
    }
    
    vector<bool> visited(net.getNumPins(), false);
    robin_hood::unordered_map<int, std::shared_ptr<SteinerTreeNode>> created;
    for (auto& solution : solutions) {
        std::shared_ptr<Solution> temp = solution;
        std::shared_ptr<SteinerTreeNode> lastNode = nullptr;
        while (temp) {
            auto it = created.find(temp->vertex);
            if (it == created.end()) {
                utils::PointT<int> point = graph.getPoint(temp->vertex);
                auto node = std::make_shared<SteinerTreeNode>(point);
                created.emplace(temp->vertex, node);
                if (lastNode) node->children.emplace_back(lastNode);
                if (!temp->prev) tree = node;
                if (!lastNode || !temp->prev) {
                    // Both the start and the end of the path should contain pins
                    int pinIndex = graph.getVertexPin(temp->vertex);
                    assert(pinIndex != -1);
                    node->fixedLayers = graph.getPseudoPin(pinIndex).second;
                }
                lastNode = node;
                temp = temp->prev;
            } else {
                if (lastNode) it->second->children.emplace_back(lastNode);
                break;
            }
        }
    }
    
    // Remove redundant tree nodes
    SteinerTreeNode::preorder(tree, [&] (std::shared_ptr<SteinerTreeNode> node) {
        for (int childIndex = 0; childIndex < node->children.size(); childIndex++) {
            std::shared_ptr<SteinerTreeNode> child = node->children[childIndex];
            if (node->x == child->x && node->y == child->y) {
                for (auto& gradchild : child->children) node->children.emplace_back(gradchild);
                if (child->fixedLayers.IsValid()) {
                    if (node->fixedLayers.IsValid()) node->fixedLayers.UnionWith(child->fixedLayers);
                    else node->fixedLayers = child->fixedLayers;
                }
                node->children.erase(node->children.begin() + childIndex);
                childIndex -= 1;
            }
        }
    });
    
    // Remove intermediate tree nodes
    SteinerTreeNode::preorder(tree, [&] (std::shared_ptr<SteinerTreeNode> node) {
        for (std::shared_ptr<SteinerTreeNode>& child : node->children) {
            unsigned direction = (node->y == child->y ? MetalLayer::H : MetalLayer::V);
            std::shared_ptr<SteinerTreeNode> temp = child;
            while(
                !temp->fixedLayers.IsValid() && temp->children.size() == 1 &&
                (*temp)[1 - direction] == (*(temp->children[0]))[1 - direction]
            ) temp = temp->children[0];
            child = temp;
        }
    });
    
    // Check duplicate tree nodes
    SteinerTreeNode::preorder(tree, [&] (std::shared_ptr<SteinerTreeNode> node) {
        for (auto child : node->children) {
            if (node->x == child->x && node->y == child->y) {
                log() << "Error: duplicate tree nodes encountered." << std::endl;
            }
        }
    });
    return tree;
}
namespace {

std::shared_ptr<GRTreeNode> searchLayers(const GRNet& net, const GridGraph& graph,
                                         int minLayer, const GRPoint* landing, int stride, int margin) {
    const auto& pins = net.getPinAccessPoints();
    if (pins.empty()) return nullptr;
    std::array<vector<int>, 2> coordinates;
    for (int axis = 0; axis < 2; ++axis) {
        const int size = graph.getSize(axis);
        const auto& bounds = net.getBoundingBox()[axis];
        const int low = margin < 0 ? 0 : std::max(0, bounds.low - margin);
        const int high = margin < 0 ? size - 1 : std::min(size - 1, bounds.high + margin);
        for (int line = low; line < high; line += stride) coordinates[axis].push_back(line);
        for (const auto& pin : pins) {
            for (const auto& access : pin) coordinates[axis].push_back(access[axis]);
        }
        auto& lines = coordinates[axis];
        lines.push_back(high);
        std::sort(lines.begin(), lines.end());
        lines.erase(std::unique(lines.begin(), lines.end()), lines.end());
    }

    const auto& xs = coordinates[0];
    const auto& ys = coordinates[1];
    const std::size_t plane = xs.size() * ys.size();
    const int layers = graph.getNumLayers();
    const std::size_t numVertices = plane * layers;
    auto vertex = [&](const GRPoint& point) {
        const auto xi = std::lower_bound(xs.begin(), xs.end(), point.x) - xs.begin();
        const auto yi = std::lower_bound(ys.begin(), ys.end(), point.y) - ys.begin();
        return point.layerIdx * plane + yi * xs.size() + xi;
    };
    auto position = [&](std::size_t index) {
        return GRPoint(index / plane, xs[index % plane % xs.size()], ys[index % plane / xs.size()]);
    };

    robin_hood::unordered_map<std::size_t, vector<std::size_t>> pinVertices;
    for (std::size_t pin = 0; pin < pins.size(); ++pin) {
        for (const auto& access : pins[pin]) pinVertices[vertex(access)].push_back(pin);
    }
    vector<bool> connected(pins.size(), false);
    std::size_t remaining = pins.size();
    auto attach = [&](std::size_t index) {
        auto found = pinVertices.find(index);
        if (found == pinVertices.end()) return;
        for (auto pin : found->second) {
            if (!connected[pin]) {
                connected[pin] = true;
                --remaining;
            }
        }
    };

    const auto source = vertex(landing ? *landing : pins.front().front());
    auto tree = std::make_shared<GRTreeNode>(position(source));
    robin_hood::unordered_map<std::size_t, std::shared_ptr<GRTreeNode>> treeNodes;
    treeNodes.emplace(source, tree);
    attach(source);
    const auto none = std::numeric_limits<std::size_t>::max();
    vector<CostT> costs(numVertices, std::numeric_limits<CostT>::infinity());
    vector<std::size_t> prev(numVertices, none);

    using QueueItem = std::pair<CostT, std::size_t>;
    std::priority_queue<QueueItem, vector<QueueItem>, std::greater<QueueItem>> queue;
    costs[source] = 0;
    queue.emplace(0, source);
    while (remaining && !queue.empty()) {
        const auto current = queue.top();
        queue.pop();
        const auto index = current.second;
        if (current.first > costs[index]) continue;
        const auto before = remaining;
        attach(index);
        if (before != remaining && !treeNodes.count(index)) {
            vector<std::size_t> path;
            auto next = index;
            while (!treeNodes.count(next)) {
                path.push_back(next);
                next = prev[next];
            }
            auto parent = treeNodes.at(next);
            for (auto it = path.rbegin(); it != path.rend(); ++it) {
                auto node = std::make_shared<GRTreeNode>(position(*it));
                parent->children.push_back(node);
                treeNodes.emplace(*it, node);
                parent = node;
                costs[*it] = 0;
                queue.emplace(0, *it);
                attach(*it);
            }
            continue;
        }

        const auto point = position(index);
        auto relax = [&](std::size_t next, CostT edgeCost) {
            const CostT cost = current.first + edgeCost;
            if (cost >= costs[next]) return;
            costs[next] = cost;
            prev[next] = index;
            queue.emplace(cost, next);
        };
        if (point.layerIdx >= minLayer) {
            const unsigned axis = graph.getLayerDirection(point.layerIdx);
            const auto slot = axis == MetalLayer::H ? index % plane % xs.size() : index % plane / xs.size();
            const auto step = axis == MetalLayer::H ? 1 : xs.size();
            if (slot > 0) {
                relax(index - step, graph.getWireCost(point.layerIdx, point, position(index - step)));
            }
            if (slot + 1 < coordinates[axis].size()) {
                relax(index + step, graph.getWireCost(point.layerIdx, point, position(index + step)));
            }
        }
        if (point.layerIdx > 0 && graph.canUseVia(point.layerIdx - 1)) {
            relax(index - plane, graph.getViaCost(point.layerIdx - 1, point));
        }
        if (point.layerIdx + 1 < layers && graph.canUseVia(point.layerIdx)) {
            relax(index + plane, graph.getViaCost(point.layerIdx, point));
        }
    }
    return remaining ? nullptr : tree;
}

} // namespace

std::shared_ptr<GRTreeNode> routeLayeredMaze(const GRNet& net, const GridGraph& graph,
                                          int minLayer, const GRPoint* landing) {
    for (const auto& sampling : {std::make_pair(10, 15), std::make_pair(4, 32),
                                 std::make_pair(10, -1), std::make_pair(1, -1)}) {
        auto tree = searchLayers(net, graph, minLayer, landing, sampling.first, sampling.second);
        if (tree) return tree;
    }
    return nullptr;
}
