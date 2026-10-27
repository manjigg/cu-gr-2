// 文件功能：构造包含引脚坐标的稀疏方向图，以优先队列扩展最短路径并连接全部伪引脚。
// 回溯搜索状态，合并共享路径、重合点和共线中间点，生成可交给模式布线器的拓扑树。
//
// 【从例子理解】已有 A->B 的连通路线后，要接 C，不必又单独算 C->A。
// 可以从整条 A->B 上任一点出发找 C，把接入距离短的地方作为分叉点。
// 搜索优先弹出累计分最小的状态，并用 minCosts 剪掉到同一点却更贵的走法。
// 新接入路径作为零代价源，表示“这部分已经是公共连通树，无需重新计连接代价”。
// SparseGraph::init 建搜索地图；run 接引脚；getSteinerTree 还原并简化结果。
// 原实现假定接入点/图存在并可连通；注释说明流程，不增加新的异常处理逻辑。
#include <queue>
#include "MazeRoute.h"

// 常规采样线之外强制加入引脚 x/y 坐标，保证每个伪引脚都有搜索顶点。
void SparseGraph::init(GridGraphView<CostT>& wireCostView, SparseGrid& grid) {
    // selectedAccessPoints 选中位置表；pseudoPins 合并后的端点；pxs/pys 原引脚轴坐标列表。
    // xSize/ySize 原图格子数，xs/ys 最终采样轴坐标；i 规则采样序号，j 尚未合并的引脚轴位置。
    // x/y 当前原坐标，xi/yi 在 xs/ys 内的下标，direction 搜索平面 H/V。
    // addSameLayerEdge：u/v 两端顶点编号，U/V 两端原图二维坐标，大写小写含义不同。
    // addDiffLayerEdge：u 为 H 顶点，v 为同位置 V 顶点，编号相差 xs.size()*ys.size()。
    // xtoxi/ytoyi：“原坐标->采样下标”字典；pinIndex 伪引脚下标，pin 它的坐标/层区间记录。
    // 同一二维格子上的接入点合并为伪引脚；真实层要求仍保存在其层区间中。
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
    // 将引脚 x/y 分别排序，随后可与规则采样线按大小合并并去掉重复坐标。
    std::sort(pxs.begin(), pxs.end());
    std::sort(pys.begin(), pys.end());
    
    const int xSize = gridGraph.getSize(0);
    const int ySize = gridGraph.getSize(1);
    xs.reserve(xSize / grid.interval.x + pxs.size());
    ys.reserve(ySize / grid.interval.y + pys.size());
    int j = 0;
    for (int i = 0; true; i++) {
        // 例：interval.x=10、offset.x=2，规则采样位置为 2/12/22/32...。
        // 若引脚在 x=7，还会另外加入 7，这样不会因为规则采样错过它。
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
    
    // 此处 GRPoint.layerIdx 暂用作方向 0/1，尚不是实际工艺金属层。
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
        // 采样点相距可能十几格，边分数需累加中间全部原网格边，不能只算一步。
        // u->v 和 v->u 的代价相同，所以一次赋给两个方向槽。
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
        // 引脚位置允许无额外二维转向分地切换方向平面。
        // 真正所需的金属层/过孔成本留给之后的 PatternRoute 处理。
        // Set the cost of the diff-layer connection at u to be 0
        costs[u][2] = 0;
        costs[u + xs.size() * ys.size()][2] = 0;
    }
}

// 从一个伪引脚开始做 Dijkstra 式扩展，每接通一个新引脚便将整条路径置为零代价源。
// 后续引脚可以接到已建树的任意位置，而不必都返回最初的引脚。
void MazeRoute::run() {
    // minCosts[v] 到搜索顶点 v 已知最小分；queue 按 cost 取最小值的候选状态队列。
    // compareSolution 比较器的 lhs/rhs 是两个状态地址；updateSolution 登记新状态并更新最低分。
    // visited[伪引脚编号] 是否已接入；startPinIndex 起始伪引脚 0；numDetached 还差多少伪引脚。
    // foundSolution 本轮找到的路径终点，foundPinIndex 该终点伪引脚编号；solution 当前弹出状态。
    // edgeIndex 0/1/2 邻边槽，不是全图边编号；nextVertex 邻顶点编号，nextCost 扩展累计分。
    // temp 是沿 foundSolution.prev 回溯的游标，已接路径逐点变成新一轮的零代价源。
    // minCosts[v] 是目前知道到顶点 v 的最低累计分数，初始设成最大可表示值。
    vector<CostT> minCosts(graph.getNumVertices(), std::numeric_limits<CostT>::max());
    solutions.reserve(net.getNumPins());
    // priority_queue 默认偏向较大值；这里用 > 比较，令较小 cost 的状态先被取出。
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
    // 从编号 0 的伪引脚建立最初连通区域。visited 表示已连接，不是已放置单元。
    const int startPinIndex = 0;
    visited[startPinIndex] = true;
    int numDetached = graph.getNumPseudoPins() - 1;
    updateSolution(std::make_shared<Solution>(0, graph.getPinVertex(startPinIndex), nullptr));
    
    // 外层循环每轮接入一个尚未连通的伪引脚，直到 numDetached 变成 0。
    while (numDetached > 0) {
        std::shared_ptr<Solution> foundSolution;
        int foundPinIndex;
        while(!queue.empty()) {
            auto solution = queue.top();
            // top 查看最便宜状态，pop 将其从队列移走，然后再扩展它的邻居。
            queue.pop();
            foundPinIndex = graph.getVertexPin(solution->vertex);
            if (foundPinIndex != -1 && !visited[foundPinIndex]) {
                foundSolution = solution;
                break;
            }
            // Pruning
            // 已有更便宜的状态抵达同一点，就不用再沿这条昂贵前缀继续搜索。
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
            // 保留路径回溯关系，但把“从已接通树出发”的累计分设为 0。
            // make_shared 分配对象并自动管理寿命，不要求手写 delete。
            updateSolution(std::make_shared<Solution>(0, temp->vertex, temp->prev));
            temp = temp->prev;
        }
    }
    
    if (numDetached != 0) {
        log() << "Error: failed to connect all pins." << std::endl;
    }
}


// 沿 prev 回溯路径，并按顶点索引共享节点；最后消除方向切换造成的重合二维节点。
std::shared_ptr<SteinerTreeNode> MazeRoute::getSteinerTree() const {
    // tree 输出二维拓扑根，pseudoPin 单点情况记录；created：“搜索顶点编号->已建拓扑点”。
    // solution 每次连接的终点状态，temp 回溯游标，lastNode 上次已建的子方向节点；
    // it 字典查找位置，point 当前顶点二维位置，node 新拓扑点，pinIndex 端点对应伪引脚。
    // visited 此方法当前只创建而没有后续读写，不能把它解释为本方法实际去重工具。
    // childIndex 正在合并的孩子槽，child 当前孩子，gradchild 原孩子的孩子（源码的拼写）；
    // direction 共线方向，temp 在末次简化循环重新指向拓扑游标，非前面 Solution 状态。
    // created 字典：搜索顶点编号 -> 已建拓扑节点；不同路径碰到同一顶点就共享。
    // 这样得到有分叉的公共树，而不是多条彼此重复的独立路径。
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
    
    // 切换方向平面会产生相同 x/y 的连续节点；投影到二维后合并它们。
    // 引脚层约束也要合并，不能只删节点而丢掉必须接入的层。
    // Remove redundant tree nodes
    SteinerTreeNode::preorder(tree, [&] (std::shared_ptr<SteinerTreeNode> node) {
        for (int childIndex = 0; childIndex < node->children.size(); childIndex++) {
            std::shared_ptr<SteinerTreeNode> child = node->children[childIndex];
            if (node->x == child->x && node->y == child->y) {
                for (auto& gradchild : child->children) node->children.emplace_back(gradchild);
                if (child->fixedLayers.IsValid()) {
                    if (node->fixedLayers.IsValid()) node->fixedLayers = node->fixedLayers.UnionWith(child->fixedLayers);
                    else node->fixedLayers = child->fixedLayers;
                }
                node->children.erase(node->children.begin() + childIndex);
                childIndex -= 1;
            }
        }
    });
    
    // A->B->C 三点共线、B 不是引脚约束且只有一个孩子时，可以压成 A->C。
    // 只减少存储节点，路线几何并不因此改成另一条道路。
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
