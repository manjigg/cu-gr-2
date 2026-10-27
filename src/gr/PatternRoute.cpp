// 文件功能：实现 FLUTE Steiner 拓扑、候选路径 DAG、拥塞绕行和联合路径/层选择动态规划。
// calculateRoutingCosts 自底向上求代价，getRoutingTree 根据 bestPaths 回溯出真实布线树。
//
// 【从一个小例子理解】A=(1,1)、B=(4,3)，不能用斜线直接连。
// 最基本的两种候选是 A->(4,1)->B 和 A->(1,3)->B，都是 L 形。
// 两种候选的平面线长相同，但经过的拥塞边不同、可用层不同，总代价未必相同。
// constructPaths 列出选择；calculateRoutingCosts 算分并记选择；getRoutingTree 执行选择。
// 多引脚线网的不同分支还要在同一位置换层连接，所以不能逐段完全独立选层。
// fixedLayers 约束此位置的过孔堆叠至少经过哪些层，动态规划会考虑这一额外要求。
// 代码里大段 /* ... */ 包住的旧构造方法不参与编译，应先阅读前面的有效版本。
#include "PatternRoute.h"

void SteinerTreeNode::preorder(
    std::shared_ptr<SteinerTreeNode> node, 
    std::function<void(std::shared_ptr<SteinerTreeNode>)> visit
) {
    // visit 是调用者传入的操作，std::function 能保存这种“可调用的函数”。
    // 函数先处理当前点，再递归处理孩子；没有孩子就停止向下。
    visit(node);
    for (auto& child : node->children) preorder(child, visit);
}

std::string SteinerTreeNode::getPythonString(std::shared_ptr<SteinerTreeNode> node) {
    vector<std::pair<utils::PointT<int>, utils::PointT<int>>> edges;
    preorder(node, [&] (std::shared_ptr<SteinerTreeNode> node) {
        for (auto child : node->children) {
            edges.emplace_back(*node, *child);
        }
    });
    std::stringstream ss;
    ss << "[";
    for (int i = 0; i < edges.size(); i++) {
        auto& edge = edges[i];
        ss << "[" << edge.first << ", " << edge.second << "]";
        ss << (i < edges.size() - 1 ? ", " : "]");
    }
    return ss.str();
}

std::string PatternRoutingNode::getPythonString(std::shared_ptr<PatternRoutingNode> routingDag) {
    vector<std::pair<utils::PointT<int>, utils::PointT<int>>> edges;
    std::function<void(std::shared_ptr<PatternRoutingNode>)> getEdges = 
        [&] (std::shared_ptr<PatternRoutingNode> node) {
            for (auto& childPaths : node->paths) {
                for (auto& path : childPaths) {
                    edges.emplace_back(*node, *path);
                    getEdges(path);
                }
            }
        };
    getEdges(routingDag);
    std::stringstream ss;
    ss << "[";
    for (int i = 0; i < edges.size(); i++) {
        auto& edge = edges[i];
        ss << "[" << edge.first << ", " << edge.second << "]";
        ss << (i < edges.size() - 1 ? ", " : "]");
    }
    return ss.str();
}

// 合并同一二维网格的接入点后调用 FLUTE，再去掉重合分支并附上引脚层约束。
void PatternRoute::constructSteinerTree() {
    // selectedAccessPoints 选中二维位置->坐标/层区间；degree 合并后点数，非原引脚数。
    // xs/ys 交给 FLUTE 的坐标数组（格子下标），i 当前填入下标；flutetree 外部树结果。
    // numBranches=2*degree-2 分支数量；branchIndex 当前分支编号；branch 一条 FLUTE 记录。
    // steinerPoints[分支编号] 位置；adjacentList[分支编号] 相邻分支表。
    // constructTree 小函数输入 parent/prevIndex/curIndex，分别为父节点、上个/当前分支编号；
    // current 当前新节点，nextIndex 邻居编号，hash 二维位置键；prevIndex 防沿无向边走回头。
    // root 所选根分支编号；hasDegree1 判断可作根的端点；index/nextIndex 为其临时遍历编号。
    // selectedAccessPoints 字典的 key 是二维格子编号，value 是“坐标+层区间”。
    // 两个物理引脚若选中同一格子，可以合成一个二维拓扑位置，再用层区间连接。
    // 1. Select access points
    robin_hood::unordered_map<uint64_t, std::pair<utils::PointT<int>, utils::IntervalT<int>>> selectedAccessPoints;
    gridGraph.selectAccessPoints(net, selectedAccessPoints);
    
    // 2. Construct Steiner tree
    // degree 是合并后的二维位置个数，不一定等于原网表引脚数。
    const int degree = selectedAccessPoints.size();
    if (degree == 1) {
        for (auto& accessPoint : selectedAccessPoints) {
            steinerTree = std::make_shared<SteinerTreeNode>(accessPoint.second.first, accessPoint.second.second);
        }
    } else {
        // 准备 FLUTE 需要的 x/y 数组；依次填入每个选中位置的坐标。
        // 这里是原实现的运行时长度数组写法，当前编译器接受这一扩展。
        int xs[degree * 4];
        int ys[degree * 4];
        int i = 0;
        for (auto& accessPoint : selectedAccessPoints) {
            xs[i] = accessPoint.second.first.x;
            ys[i] = accessPoint.second.first.y;
            i++;
        }
        // 调用外部 FLUTE 库。Tree 中 branch[i].n 表示分支 i 连向哪个分支编号。
        // ACCURACY 是库中配置的精度，不是输入文件中的工艺精度。
        Tree flutetree = flute(degree, xs, ys, ACCURACY);
        const int numBranches = degree + degree - 2;
        vector<utils::PointT<int>> steinerPoints;
        steinerPoints.reserve(numBranches);
        // 邻接表：adjacentList[i] 列出与分支 i 相邻的所有分支。
        // 先构成无向关系，再选根并递归转成单向 children 树。
        vector<vector<int>> adjacentList(numBranches);
        for (int branchIndex = 0; branchIndex < numBranches; branchIndex++) {
            const Branch& branch = flutetree.branch[branchIndex];
            steinerPoints.emplace_back(branch.x, branch.y);
            if (branchIndex == branch.n) continue;
            adjacentList[branchIndex].push_back(branch.n);
            adjacentList[branch.n].push_back(branchIndex);
        }
        std::function<void(std::shared_ptr<SteinerTreeNode>&, int, int)> constructTree = [&] (
            std::shared_ptr<SteinerTreeNode>& parent, int prevIndex, int curIndex
        ) {
            std::shared_ptr<SteinerTreeNode> current = std::make_shared<SteinerTreeNode>(steinerPoints[curIndex]);
            // 若当前点与父点同坐标，跳过重复点，直接把后续分支接到父点。
            // 这不是丢弃真正连接，只是压缩零长度节点。
            if (parent != nullptr && parent->x == current->x && parent->y == current->y) {
                for (int nextIndex : adjacentList[curIndex]) {
                    if (nextIndex == prevIndex) continue;
                    constructTree(parent, curIndex, nextIndex);
                }
                return;
            }
            // Build subtree
            for (int nextIndex : adjacentList[curIndex]) {
                if (nextIndex == prevIndex) continue;
                constructTree(current, curIndex, nextIndex);
            }
            // Set fixed layer interval
            uint64_t hash = gridGraph.hashCell(current->x, current->y);
            if (selectedAccessPoints.find(hash) != selectedAccessPoints.end()) {
                current->fixedLayers = selectedAccessPoints[hash].second;
            }
            // Connect current to parent
            if (parent == nullptr) {
                parent = current;
            } else {
                parent->children.emplace_back(current);
            }
        };
        // 选一端点做根，有助于后面的绕行构造；树的连接关系本身不因选根改变。
        // Pick a root having degree 1
        int root = 0;
        std::function<bool(int)> hasDegree1 = [&] (int index) {
            if (adjacentList[index].size() == 1) {
                int nextIndex = adjacentList[index][0];
                if (steinerPoints[index] == steinerPoints[nextIndex]) {
                    return hasDegree1(nextIndex);
                } else {
                    return true;
                }
            } else {
                return false;
            }
        };
        for (int i = 0; i < steinerPoints.size(); i++) {
            if (hasDegree1(i)) {
                root = i;
                break;
            }
        }
        constructTree(steinerTree, -1, root);
        free(flutetree.branch);
    }
}

// 将拓扑边展开成候选路径；对角端点提供两种 L 形拐弯方向。
void PatternRoute::constructRoutingDAG() {
    // constructDag 输入 dstNode 父节点引用、steiner 原拓扑当前点；current 为新候选图节点。
    // steinerChild 当前拓扑孩子。递归完成孩子后，连接 current 并调用 constructPaths 增候选。
    // lambda constructDag 递归复制拓扑位置，给每个 DAG 节点分配唯一 index。
    // 原树连接记进 children，而 constructPaths 给这条连接添加实际走法候选。
    std::function<void(std::shared_ptr<PatternRoutingNode>&, std::shared_ptr<SteinerTreeNode>&)> constructDag = [&] (
        std::shared_ptr<PatternRoutingNode>& dstNode, std::shared_ptr<SteinerTreeNode>& steiner
    ) {
        std::shared_ptr<PatternRoutingNode> current = std::make_shared<PatternRoutingNode>(
            *steiner, steiner->fixedLayers, numDagNodes++
        );
        for (auto steinerChild : steiner->children) {
            constructDag(current, steinerChild);
        }
        if (dstNode == nullptr) {
            dstNode = current;
        } else {
            dstNode->children.emplace_back(current);
            constructPaths(dstNode, current);
        }
    };
    constructDag(routingDag, steinerTree);
}

/*
void PatternRoute::constructRoutingDAG() {
    // 1. Select access points
    robin_hood::unordered_map<uint64_t, std::pair<utils::PointT<int>, utils::IntervalT<int>>> selectedAccessPoints;
    // cell hash (2d) -> access point, fixed layer interval
    selectedAccessPoints.reserve(net.getNumPins());
    const auto& boundingBox = net.getBoundingBox();
    utils::PointT<int> netCenter(boundingBox.cx(), boundingBox.cy());
    for (const auto& accessPoints : net.getPinAccessPoints()) {
        std::pair<int, int> bestAccessDist = {0, std::numeric_limits<int>::max()};
        int bestIndex = -1;
        for (int index = 0; index < accessPoints.size(); index++) {
            const auto& point = accessPoints[index];
            int accessibility = 0;
            if (point.layerIdx >= parameters.min_routing_layer) {
                unsigned direction = gridGraph.getLayerDirection(point.layerIdx);
                accessibility += gridGraph.getEdge(point.layerIdx, point.x, point.y).capacity >= 1;
                if (point[direction] > 0) {
                    auto lower = point;
                    lower[direction] -= 1;
                    accessibility += gridGraph.getEdge(lower.layerIdx, lower.x, lower.y).capacity >= 1;
                }
            } else {
                accessibility = 1;
            }
            int distance = abs(netCenter.x - point.x) + abs(netCenter.y - point.y);
            if (accessibility > bestAccessDist.first || (accessibility == bestAccessDist.first && distance < bestAccessDist.second)) {
                bestIndex = index;
                bestAccessDist = {accessibility, distance};
            }
        }
        if (bestAccessDist.first == 0) {
            log() << "Warning: the pin is hard to access." << std::endl; 
            for (auto pt : accessPoints) {
                // log() << gridGraph.getSize(0) << ", " << gridGraph.getSize(1) << std::endl;
                log() << gridGraph.getCellBox(pt).x.low / 2000.0 << " " << gridGraph.getCellBox(pt).y.low / 2000.0 << " " << gridGraph.getCellBox(pt).x.high / 2000.0 << " " << gridGraph.getCellBox(pt).y.high / 2000.0 << std::endl;
            }
        }
        const utils::PointT<int> selectedPoint = accessPoints[bestIndex];
        const uint64_t hash = gridGraph.hashCell(selectedPoint.x, selectedPoint.y);
        if (selectedAccessPoints.find(hash) == selectedAccessPoints.end()) {
            selectedAccessPoints.emplace(hash, std::make_pair(selectedPoint, utils::IntervalT<int>()));
        }
        utils::IntervalT<int>& fixedLayerInterval = selectedAccessPoints[hash].second;
        for (const auto& point : accessPoints) {
            if (point.x == selectedPoint.x && point.y == selectedPoint.y) {
                fixedLayerInterval.Update(point.layerIdx);
            }
        }
    }
    // Extend the fixed layers to 2 layers higher to facilitate track switching
    for (auto& accessPoint : selectedAccessPoints) {
        utils::IntervalT<int>& fixedLayers = accessPoint.second.second;
        fixedLayers.high = min(fixedLayers.high + 2, (int)gridGraph.getNumLayers() - 1);
    }
    // 2. Construct Steiner tree
    const int degree = selectedAccessPoints.size();
    if (degree == 1) {
        for (auto& accessPoint : selectedAccessPoints) {
            routingDag = std::make_shared<PatternRoutingNode>(accessPoint.second.first, accessPoint.second.second, numDagNodes++);
        }
    } else {
        int xs[degree * 4];
        int ys[degree * 4];
        int i = 0;
        for (auto& accessPoint : selectedAccessPoints) {
            xs[i] = accessPoint.second.first.x;
            ys[i] = accessPoint.second.first.y;
            i++;
        }
        Tree flutetree = flute(degree, xs, ys, ACCURACY);
        const int numBranches = degree + degree - 2;
        vector<utils::PointT<int>> steinerPoints;
        steinerPoints.reserve(numBranches);
        vector<vector<int>> adjacentList(numBranches);
        for (int branchIndex = 0; branchIndex < numBranches; branchIndex++) {
            const Branch& branch = flutetree.branch[branchIndex];
            steinerPoints.emplace_back(branch.x, branch.y);
            if (branchIndex == branch.n) continue;
            adjacentList[branchIndex].push_back(branch.n);
            adjacentList[branch.n].push_back(branchIndex);
        }
        std::function<void(std::shared_ptr<PatternRoutingNode>&, int, int)> constructNodes = 
        [&](std::shared_ptr<PatternRoutingNode>& parent, int prevIndex, int curIndex) {
            std::shared_ptr<PatternRoutingNode> current = std::make_shared<PatternRoutingNode>(steinerPoints[curIndex], numDagNodes++);
            if (parent != nullptr && parent->x == current->x && parent->y == current->y) {
                for (int nextIndex : adjacentList[curIndex]) {
                    if (nextIndex == prevIndex) continue;
                    constructNodes(parent, curIndex, nextIndex);
                }
                return;
            }
            // Build subtree
            for (int nextIndex : adjacentList[curIndex]) {
                if (nextIndex == prevIndex) continue;
                constructNodes(current, curIndex, nextIndex);
            }
            // Set fixed layer interval
            uint64_t hash = gridGraph.hashCell(current->x, current->y);
            if (selectedAccessPoints.find(hash) != selectedAccessPoints.end()) {
                current->fixedLayers = selectedAccessPoints[hash].second;
            }
            // Connect current to parent
            if (parent == nullptr) {
                parent = current;
            } else {
                parent->children.emplace_back(current);
                constructPaths(parent, current);
            }
        };
        int root = 0;
        // pick a root has a degree of 1
        std::function<bool(int)> hasDegree1 = [&] (int index) {
            if (adjacentList[index].size() == 1) {
                int nextIndex = adjacentList[index][0];
                if (steinerPoints[index] == steinerPoints[nextIndex]) {
                    return hasDegree1(nextIndex);
                } else {
                    return true;
                }
            } else {
                return false;
            }
        };
        for (int i = 0; i < steinerPoints.size(); i++) {
            if (hasDegree1(i)) {
                root = i;
                break;
            }
        }
        constructNodes(routingDag, -1, root);
    }
}
*/

// childIndex 指定候选归属的树分支；-1 表示新增分支候选集合。
void PatternRoute::constructPaths(std::shared_ptr<PatternRoutingNode>& start, std::shared_ptr<PatternRoutingNode>& end, int childIndex) {
    // childPaths 是 start->paths[childIndex] 的可写引用；pathIndex=0/1 是两个 L 形序号。
    // midPoint 当前候选拐点位置，mid 新拐点节点；它 paths[0][0] 直达 end。
    // 参数 shared_ptr<...>& 是“借用指针变量”，不是复制整棵图。
    // start/end 是两端，childIndex 指明正在给 start 的哪一个孩子补充候选。
    if (childIndex == -1) {
        childIndex = start->paths.size();
        start->paths.emplace_back();
    }
    vector<std::shared_ptr<PatternRoutingNode>>& childPaths = start->paths[childIndex];
    // 任一坐标相等说明能直线连，只需把 end 作为下一跳。
    if (start->x == end->x || start->y == end->y) {
        childPaths.push_back(end);
    } else {
        for (int pathIndex = 0; pathIndex <= 1; pathIndex++) { // two paths of different L-shape
            utils::PointT<int> midPoint = pathIndex ? utils::PointT<int>(start->x, end->y) : utils::PointT<int>(end->x, start->y);
            std::shared_ptr<PatternRoutingNode> mid = std::make_shared<PatternRoutingNode>(midPoint, numDagNodes++, true);
            // 拐点之后只有一条连接和一种走法：直接到 end，所以是两层花括号。
            mid->paths = {{end}};
            childPaths.push_back(mid);
        }
    }
}

// 将拥塞的同方向片段组织为 scaffold，尝试平移主干并以支线连接原有端点。
// 绕行长度增量和候选数量受 Parameters 限制；新路径加入 DAG 供统一比较。
void PatternRoute::constructDetours(GridGraphView<bool>& congestionView) {
    // 【绕行变量】ScaffoldNode.node 原 DAG 点，children 骨架孩子，不是最终导线孩子。
    // scaffolds[H/V] 各方向骨架根列表；scaffoldNodes[direction][原节点编号] 暂存待合并骨架。
    // visited[原编号] 防 DAG 共享点重复扫描；buildScaffolds 找拥塞骨架；path 当前候选下一跳。
    // getTrunkAndStems 收集 trunk 主干走线轴范围和 stems 外接端点垂直轴位置；
    // starting 区分骨架入口与其后节点；scaffolded 表示该树孩子已在骨架内。
    // getTotalStemLength(stems,pos) 求主干在 pos 时各支线长度之和，length 是累计格子长度。
    // buildDetour(scaffoldNode,direction,shiftAmount) 创建平移后的候选；treeNode 原节点；
    // dupTreeNode 复制的固定引脚端点，shiftedTreeNode 平移点，shiftedChildTreeNode 平移孩子；
    // built 标记该孩子是否已按骨架改造。带 fixedLayers 的端点留在原位，以支线接到平移点。
    // trunkPos 原主干垂直轴位置；originalLength 原支线总长；maxLengthIncrease 可增加预算；
    // shiftInterval 允许候选坐标区间，step 采样间隔，pos 当前采样位置，shiftAmount 位移格数。
    // dupShiftInterval 当前仅保留一份区间副本，后续没有使用，勿为它附会新算法作用。
    // scaffold/scaffoldChild 当前骨架根/孩子；childIndex 原树孩子槽，shiftedChild 新替代候选。
    // 可把 scaffold 看作“准备挪动的一段树骨架”，暂时把相关同向拥塞边归成组。
    // buildScaffolds 找组；getTrunkAndStems 估计挪动代价；buildDetour 生成挪动后的候选。
    // 这里增加的是可选路径，并不直接确定最终导线必须绕到哪里。
    struct ScaffoldNode {
        std::shared_ptr<PatternRoutingNode> node;
        vector<std::shared_ptr<ScaffoldNode>> children;
        ScaffoldNode(std::shared_ptr<PatternRoutingNode> n): node(n) {}
    };
    
    vector<vector<std::shared_ptr<ScaffoldNode>>> scaffolds(2);
    vector<vector<std::shared_ptr<ScaffoldNode>>> scaffoldNodes(
        2, vector<std::shared_ptr<ScaffoldNode>>(numDagNodes, nullptr)
    ); // direction -> numDagNodes -> scaffold node
    // DAG 有共享节点，visited 标记已处理编号，避免同一骨架节点重复处理。
    vector<bool> visited(numDagNodes, false);
    
    std::function<void(std::shared_ptr<PatternRoutingNode>)> buildScaffolds = 
        [&] (std::shared_ptr<PatternRoutingNode> node) {
            if (visited[node->index]) return;
            visited[node->index] = true;
            
            if (node->optional) {
                assert(node->paths.size() == 1 && node->paths[0].size() == 1 && !node->paths[0][0]->optional);
                auto& path = node->paths[0][0];
                buildScaffolds(path);
                unsigned direction = (node->y == path->y ? MetalLayer::H : MetalLayer::V);
                if (!scaffoldNodes[direction][path->index] && congestionView.check(*node, *path)) {
                    scaffoldNodes[direction][path->index] = std::make_shared<ScaffoldNode>(path);
                }
            } else {
                for (auto& childPaths : node->paths) {
                    for (auto& path : childPaths) {
                        buildScaffolds(path);
                        unsigned direction = (node->y == path->y ? MetalLayer::H : MetalLayer::V);
                        if (path->optional) {
                            if (!scaffoldNodes[direction][node->index] && congestionView.check(*node, *path)) {
                                scaffoldNodes[direction][node->index] = std::make_shared<ScaffoldNode>(node);
                            }
                        } else {
                            if (congestionView.check(*node, *path)) {
                                if (!scaffoldNodes[direction][node->index]) {
                                    scaffoldNodes[direction][node->index] = std::make_shared<ScaffoldNode>(node);
                                }
                                if (!scaffoldNodes[direction][path->index]) {
                                    scaffoldNodes[direction][node->index]->children.emplace_back(std::make_shared<ScaffoldNode>(path));
                                } else {
                                    scaffoldNodes[direction][node->index]->children.emplace_back(scaffoldNodes[direction][path->index]);
                                    scaffoldNodes[direction][path->index] = nullptr;
                                }
                            }
                        }
                    }
                    for (auto& child : node->children) {
                        for (unsigned direction = 0; direction < 2; direction++) {
                            if (scaffoldNodes[direction][child->index]) {
                                scaffolds[direction].emplace_back(std::make_shared<ScaffoldNode>(node));
                                scaffolds[direction].back()->children.emplace_back(scaffoldNodes[direction][child->index]);
                                scaffoldNodes[direction][child->index] = nullptr;
                            }
                        }
                    }
                }
            }
        };
        
    buildScaffolds(routingDag);
    for (unsigned direction = 0; direction < 2; direction++) {
        if (scaffoldNodes[direction][routingDag->index]) {
            scaffolds[direction].emplace_back(std::make_shared<ScaffoldNode>(nullptr));
            scaffolds[direction].back()->children.emplace_back(scaffoldNodes[direction][routingDag->index]);
        }
    }
    
    
    // std::function<void(std::shared_ptr<ScaffoldNode>)> printScaffold = 
    //     [&] (std::shared_ptr<ScaffoldNode> scaffoldNode) {
    // 
    //         for (auto& scaffoldChild : scaffoldNode->children) {
    //             if (scaffoldNode->node) std::cout << (utils::PointT<int>)*scaffoldNode->node << " " << scaffoldNode->children.size();
    //             else std::cout << "null";
    //             std::cout << " -> " << (utils::PointT<int>)*scaffoldChild->node;
    //             if (scaffoldNode->node) std::cout << " " << congested(scaffoldNode->node, scaffoldChild->node);
    //             std::cout << std::endl;
    //             printScaffold(scaffoldChild);
    //         }
    //     };
    // 
    // for (unsigned direction = 0; direction < 1; direction++) {
    //     for (auto scaffold : scaffolds[direction]) {
    //         printScaffold(scaffold);
    //         std::cout << std::endl;
    //     }
    // }
    
    std::function<void(std::shared_ptr<ScaffoldNode>, utils::IntervalT<int>&, vector<int>&, unsigned, bool)> getTrunkAndStems = 
        [&] (std::shared_ptr<ScaffoldNode> scaffoldNode, utils::IntervalT<int>& trunk, vector<int>& stems, unsigned direction, bool starting) {
            if (starting) {
                if (scaffoldNode->node) {
                    stems.emplace_back((*scaffoldNode->node)[1 - direction]);
                    trunk.Update((*scaffoldNode->node)[direction]);
                }
                for (auto& scaffoldChild : scaffoldNode->children) getTrunkAndStems(scaffoldChild, trunk, stems, direction, false);
            } else {
                trunk.Update((*scaffoldNode->node)[direction]);
                if (scaffoldNode->node->fixedLayers.IsValid()) {
                    stems.emplace_back((*scaffoldNode->node)[1 - direction]);
                }
                for (auto& treeChild : scaffoldNode->node->children) {
                    bool scaffolded = false;
                    for (auto& scaffoldChild : scaffoldNode->children) {
                        if (treeChild == scaffoldChild->node) {
                            getTrunkAndStems(scaffoldChild, trunk, stems, direction, false);
                            scaffolded = true;
                            break;
                        }
                    }
                    if (!scaffolded) {
                        stems.emplace_back((*treeChild)[1 - direction]);
                        trunk.Update((*treeChild)[direction]);
                    }
                }
            }
        };
    
    // 主干平移到 pos 后，每条支线都需接回原端点；累加它们的距离衡量长度增量。
    // 例如两个支线坐标 1/5，主干放在 3，总支线长度 |1-3|+|5-3|=4。
    auto getTotalStemLength = [&] (const vector<int>& stems, const int pos) {
        int length = 0;
        for (int stem : stems) length += abs(stem - pos);
        return length;
    };
    
    std::function<std::shared_ptr<PatternRoutingNode>(std::shared_ptr<ScaffoldNode>, unsigned, int)> buildDetour = 
        [&] (std::shared_ptr<ScaffoldNode> scaffoldNode, unsigned direction, int shiftAmount) {
            std::shared_ptr<PatternRoutingNode> treeNode = scaffoldNode->node;
            if (treeNode->fixedLayers.IsValid()) {
                std::shared_ptr<PatternRoutingNode> dupTreeNode = 
                    std::make_shared<PatternRoutingNode>((utils::PointT<int>)*treeNode, treeNode->fixedLayers, numDagNodes++);
                std::shared_ptr<PatternRoutingNode> shiftedTreeNode = 
                    std::make_shared<PatternRoutingNode>((utils::PointT<int>)*treeNode, numDagNodes++);
                // 横向主干要上下移，纵向主干要左右移，因此改的是 1-direction 轴。
                (*shiftedTreeNode)[1 - direction] += shiftAmount;
                constructPaths(shiftedTreeNode, dupTreeNode);
                for (auto& treeChild : treeNode->children) {
                    bool built = false;
                    for (auto& scaffoldChild : scaffoldNode->children) {
                        if (treeChild == scaffoldChild->node) {
                            auto shiftedChildTreeNode = buildDetour(scaffoldChild, direction, shiftAmount);
                            constructPaths(shiftedTreeNode, shiftedChildTreeNode);
                            built = true;
                            break;
                        }
                    }
                    if (!built) {
                        constructPaths(shiftedTreeNode, treeChild);
                    }
                }
                return shiftedTreeNode;
            } else {
                std::shared_ptr<PatternRoutingNode> shiftedTreeNode = 
                    std::make_shared<PatternRoutingNode>((utils::PointT<int>)*treeNode, numDagNodes++);
                (*shiftedTreeNode)[1 - direction] += shiftAmount;
                for (auto& treeChild : treeNode->children) {
                    bool built = false;
                    for (auto& scaffoldChild : scaffoldNode->children) {
                        if (treeChild == scaffoldChild->node) {
                            auto shiftedChildTreeNode = buildDetour(scaffoldChild, direction, shiftAmount);
                            constructPaths(shiftedTreeNode, shiftedChildTreeNode);
                            built = true;
                            break;
                        }
                    }
                    if (!built) {
                        constructPaths(shiftedTreeNode, treeChild);
                    }
                }
                return shiftedTreeNode;
            }
        };
        
    for (unsigned direction = 0; direction < 2; direction++) {
        for (std::shared_ptr<ScaffoldNode> scaffold : scaffolds[direction]) {
            assert (scaffold->children.size() == 1);
            
            utils::IntervalT<int> trunk;
            vector<int> stems;
            getTrunkAndStems(scaffold, trunk, stems, direction, true);
            std::sort(stems.begin(), stems.end());
            int trunkPos = (*scaffold->children[0]->node)[1 - direction];
            int originalLength = getTotalStemLength(stems, trunkPos);
            utils::IntervalT<int> shiftInterval(trunkPos);
            // 允许的增长预算：主干范围×比例。每个采样位置需在芯片界内。
            // 下方 while 条件按当前源码实际表达式执行，不等同于严格最优绕行搜索。
            int maxLengthIncrease = trunk.range() * parameters.max_detour_ratio;
            while (shiftInterval.low - 1 >= 0 && getTotalStemLength(stems, shiftInterval.low - 1) - originalLength <= maxLengthIncrease) shiftInterval.low--;
            while (shiftInterval.high + 1 < gridGraph.getSize(1 - direction) && getTotalStemLength(stems, shiftInterval.high + 1) - originalLength <= maxLengthIncrease) shiftInterval.high++;
            int step = 1;
            while ((trunkPos - shiftInterval.low) / (step + 1) + (shiftInterval.high - trunkPos) / (step + 1) >= parameters.target_detour_count) step++;
            utils::IntervalT<int> dupShiftInterval = shiftInterval;
            shiftInterval.low = trunkPos - (trunkPos - shiftInterval.low) / step * step;
            shiftInterval.high = trunkPos + (shiftInterval.high - trunkPos) / step * step;
            for (double pos = shiftInterval.low; pos <= shiftInterval.high; pos += step) {
                int shiftAmount = (pos - trunkPos); 
                if (shiftAmount == 0) continue;
                if (scaffold->node) {
                    auto& scaffoldChild = scaffold->children[0];
                    if ((*scaffoldChild->node)[1 - direction] + shiftAmount < 0 || 
                        (*scaffoldChild->node)[1 - direction] + shiftAmount >= gridGraph.getSize(1 - direction)) {
                        continue;
                    }
                    for (int childIndex = 0; childIndex < scaffold->node->children.size(); childIndex++) {
                        auto& treeChild = scaffold->node->children[childIndex];
                        if (treeChild == scaffoldChild->node) {
                            std::shared_ptr<PatternRoutingNode> shiftedChild = buildDetour(scaffoldChild, direction, shiftAmount);
                            constructPaths(scaffold->node, shiftedChild, childIndex);
                        }
                    }
                } else {
                    std::shared_ptr<ScaffoldNode> scaffoldNode = scaffold->children[0];
                    auto treeNode = scaffoldNode->node;
                    if (treeNode->children.size() == 1) {
                        if ((*treeNode)[1 - direction] + shiftAmount < 0 || 
                            (*treeNode)[1 - direction] + shiftAmount >= gridGraph.getSize(1 - direction)) {
                            continue;
                        }
                        std::shared_ptr<PatternRoutingNode> shiftedTreeNode = 
                            std::make_shared<PatternRoutingNode>((utils::PointT<int>)*treeNode, numDagNodes++);
                        (*shiftedTreeNode)[1 - direction] += shiftAmount;
                        constructPaths(treeNode, shiftedTreeNode, 0);
                        for (auto& treeChild : treeNode->children) {
                            bool built = false;
                            for (auto& scaffoldChild : scaffoldNode->children) {
                                if (treeChild == scaffoldChild->node) {
                                    auto shiftedChildTreeNode = buildDetour(scaffoldChild, direction, shiftAmount);
                                    constructPaths(shiftedTreeNode, shiftedChildTreeNode);
                                    built = true;
                                    break;
                                }
                            }
                            if (!built) {
                                constructPaths(shiftedTreeNode, treeChild);
                            }
                        }
                    
                    } else {
                        log() << "Warning: the root has not exactly one child." << std::endl;
                    }
                }
            }
        }
    }
}

void PatternRoute::run() {
    // “求最优代价”和“建立具体结果”分开：前者缓存分数/选择，后者恢复节点连接。
    calculateRoutingCosts(routingDag);
    net.setRoutingTree(getRoutingTree(routingDag));
}

// 每个子分支先求不同层的最佳候选，再枚举覆盖 fixedLayers 的过孔层区间。
// 区间内选最便宜的子分支接入层，并用过孔前缀和快速计算换层成本。
void PatternRoute::calculateRoutingCosts(std::shared_ptr<PatternRoutingNode>& node) {
    // 【评分表】childCosts[child][layer] = (该层最小后缀+连接分,候选编号)。
    // childIndex 当前分支槽，childPaths 它的候选列表，costs 本分支分表引用。
    // pathIndex 当前候选编号，path 当前下一跳，direction 两点直连方向，cost 当前试算分。
    // layerIndex 当前真实层，viaCosts[layer] 当前点逐层过孔前缀和；fixedLayers 必连层范围。
    // lowLayerIndex 试选堆叠底层；minChildCosts[child] 当前堆叠范围内该孩子最低分；
    // bestPaths[child] 当前试选方案；它与 node->bestPaths[接入层][child] 最终存储维度不同。
    // childCost 是一个孩子的累计分值，不是列表下标；总 cost=过孔范围分+每个 childCost。
    // 输入 node 是当前子问题的根，输出存在它自己的 costs/bestPaths 字段。
    // 对每个孩子：①先算它的候选后缀；②加连接导线代价；③选各层最低的候选。
    // 再在当前点枚举可覆盖引脚约束的层区间，把过孔代价和孩子代价加在一起。
    // node->costs 非空代表已算完，不需要沿共享 DAG 再走一遍。
    // DAG 节点可能被多条候选共享，缓存避免重复求解同一个子图。
    if (node->costs.size() != 0) return;
    vector<vector<std::pair<CostT, int>>> childCosts; // childIndex -> layerIndex -> (cost, pathIndex)
    // Calculate child costs
    if (node->paths.size() > 0) childCosts.resize(node->paths.size());
    for (int childIndex = 0; childIndex < node->paths.size(); childIndex++) {
        auto& childPaths = node->paths[childIndex];
        auto& costs = childCosts[childIndex];
        // 初始“极大代价、无候选”。只有找到更便宜的可行候选才替换。
        // first 是分数，second 是候选编号；-1 是尚未选择的标记。
        costs.assign(gridGraph.getNumLayers(), {std::numeric_limits<CostT>::max(), -1});
        for (int pathIndex = 0; pathIndex < childPaths.size(); pathIndex++) {
            std::shared_ptr<PatternRoutingNode>& path = childPaths[pathIndex];
            calculateRoutingCosts(path);
            unsigned direction = node->x == path->x ? MetalLayer::V : MetalLayer::H;
            assert((*node)[1 - direction] == (*path)[1 - direction]);
            for (int layerIndex = parameters.min_routing_layer; layerIndex < gridGraph.getNumLayers(); layerIndex++) {
                // 横向线只能放在当前模型的水平首选层，纵向线只能放在垂直首选层。
                // 层不匹配就跳过，不把非法同层拐弯当成低价候选。
                if (gridGraph.getLayerDirection(layerIndex) != direction) continue;
                CostT cost = path->costs[layerIndex] + gridGraph.getWireCost(layerIndex, *node, *path);
                if (cost < costs[layerIndex].first) costs[layerIndex] = std::make_pair(cost, pathIndex);
            }
        }
    }
    
    node->costs.assign(gridGraph.getNumLayers(), std::numeric_limits<CostT>::max());
    node->bestPaths.resize(gridGraph.getNumLayers());
    if (node->paths.size() > 0) {
        for (int layerIndex = 1; layerIndex < gridGraph.getNumLayers(); layerIndex++) {
            node->bestPaths[layerIndex].assign(node->paths.size(), {-1, -1});
        }
    }
    // 前缀和：viaCosts[k] 是从索引 0 到 k 的累计过孔分。
    // 区间 [a,b] 的换层分可用 viaCosts[b]-viaCosts[a]，不必每次循环所有过孔。
    // Calculate the partial sum of the via costs
    vector<CostT> viaCosts(gridGraph.getNumLayers());
    viaCosts[0] = 0;
    for (int layerIndex = 1; layerIndex < gridGraph.getNumLayers(); layerIndex++) {
        viaCosts[layerIndex] = viaCosts[layerIndex - 1] + gridGraph.getViaCost(layerIndex - 1, *node);
    }
    utils::IntervalT<int> fixedLayers = node->fixedLayers;
    fixedLayers.low = min(fixedLayers.low, static_cast<int>(gridGraph.getNumLayers()) - 1);
    fixedLayers.high = max(fixedLayers.high, parameters.min_routing_layer);
    
    // lowLayerIndex 是试选堆叠的底层；随后逐步抬高顶层。
    // 覆盖到 fixedLayers.high 后才满足该点所有必需层的连接条件。
    for (int lowLayerIndex = 0; lowLayerIndex <= fixedLayers.low; lowLayerIndex++) {
        vector<CostT> minChildCosts; 
        vector<std::pair<int, int>> bestPaths; 
        if (node->paths.size() > 0) {
            minChildCosts.assign(node->paths.size(), std::numeric_limits<CostT>::max());
            bestPaths.assign(node->paths.size(), {-1, -1});
        }
        for (int layerIndex = lowLayerIndex; layerIndex < gridGraph.getNumLayers(); layerIndex++) {
            for (int childIndex = 0; childIndex < node->paths.size(); childIndex++) {
                if (childCosts[childIndex][layerIndex].first < minChildCosts[childIndex]) {
                    minChildCosts[childIndex] = childCosts[childIndex][layerIndex].first;
                    // 分数之外还要记住“哪条候选、哪一层”，否则最后不知道怎样画回来。
                    bestPaths[childIndex] = std::make_pair(childCosts[childIndex][layerIndex].second, layerIndex);
                }
            }
            if (layerIndex >= fixedLayers.high) {
                CostT cost = viaCosts[layerIndex] - viaCosts[lowLayerIndex];
                for (CostT childCost : minChildCosts) cost += childCost;
                if (cost < node->costs[layerIndex]) {
                    node->costs[layerIndex] = cost;
                    node->bestPaths[layerIndex] = bestPaths;
                }
            }
        }
        // 将覆盖了较高层的最佳堆叠结果向较低接入层传播，并带上相同候选选择。
        for (int layerIndex = gridGraph.getNumLayers() - 2; layerIndex >= lowLayerIndex; layerIndex--) {
            if (node->costs[layerIndex + 1] < node->costs[layerIndex]) {
                node->costs[layerIndex] = node->costs[layerIndex + 1];
                node->bestPaths[layerIndex] = node->bestPaths[layerIndex + 1];
            }
        }
    }
}

// 根节点选择最低代价层；按 bestPaths 递归接入子路径并补齐必要的上下过孔连接。
std::shared_ptr<GRTreeNode> PatternRoute::getRoutingTree(std::shared_ptr<PatternRoutingNode>& node, int parentLayerIndex) {
    // minCost 根节点最低分；routingNode 当前三维结果点；lowestRoutingNode/highestRoutingNode
    // 指向当前位置已建过孔堆叠的最低/最高结果点，方便向上下扩展。
    // pathsOnLayer[layer] 本层应接的候选下一跳列表；pathIndex/layerIndex 来自最佳选择表。
    // childIndex 分支槽，path 选中下一跳。递归结果加到正确层点的 children。
    // parentLayerIndex=-1 是根节点调用，先找 costs 中最便宜的层。
    // 普通递归则传父路径选择的层；沿 bestPaths 把候选恢复成 GRTreeNode。
    if (parentLayerIndex == -1) {
        CostT minCost = std::numeric_limits<CostT>::max();
        for (int layerIndex = 0; layerIndex < gridGraph.getNumLayers(); layerIndex++) {
            if (routingDag->costs[layerIndex] < minCost) {
                minCost = routingDag->costs[layerIndex];
                parentLayerIndex = layerIndex;
            }
        }
    }
    std::shared_ptr<GRTreeNode> routingNode = std::make_shared<GRTreeNode>(parentLayerIndex, node->x, node->y);
    std::shared_ptr<GRTreeNode> lowestRoutingNode = routingNode;
    std::shared_ptr<GRTreeNode> highestRoutingNode = routingNode;
    if (node->paths.size() > 0) {
        int pathIndex, layerIndex;
        vector<vector<std::shared_ptr<PatternRoutingNode>>> pathsOnLayer(gridGraph.getNumLayers());
        for (int childIndex = 0; childIndex < node->paths.size(); childIndex++) {
            // tie 把一对保存的数字同时取出，分别写给 pathIndex 和 layerIndex。
            std::tie(pathIndex, layerIndex) = node->bestPaths[parentLayerIndex][childIndex];
            pathsOnLayer[layerIndex].push_back(node->paths[childIndex][pathIndex]);
        }
        if (pathsOnLayer[parentLayerIndex].size() > 0) {
            for (auto& path : pathsOnLayer[parentLayerIndex]) {
                routingNode->children.push_back(getRoutingTree(path, parentLayerIndex));
            }
        }
        for (int layerIndex = parentLayerIndex - 1; layerIndex >= 0; layerIndex--) {
            if (pathsOnLayer[layerIndex].size() > 0) {
                lowestRoutingNode->children.push_back(std::make_shared<GRTreeNode>(layerIndex, node->x, node->y));
                lowestRoutingNode = lowestRoutingNode->children.back();
                for (auto& path : pathsOnLayer[layerIndex]) {
                    lowestRoutingNode->children.push_back(getRoutingTree(path, layerIndex));
                }
            }
        }
        for (int layerIndex = parentLayerIndex + 1; layerIndex < gridGraph.getNumLayers(); layerIndex++) {
            if (pathsOnLayer[layerIndex].size() > 0) {
                highestRoutingNode->children.push_back(std::make_shared<GRTreeNode>(layerIndex, node->x, node->y));
                highestRoutingNode = highestRoutingNode->children.back();
                for (auto& path : pathsOnLayer[layerIndex]) {
                    highestRoutingNode->children.push_back(getRoutingTree(path, layerIndex));
                }
            }
        }
    }
    // 如果候选连线还没触及引脚要求的最低/最高层，在同一位置补上过孔堆叠连接。
    if (lowestRoutingNode->layerIdx > node->fixedLayers.low) {
        lowestRoutingNode->children.push_back(std::make_shared<GRTreeNode>(node->fixedLayers.low, node->x, node->y));
    }
    if (highestRoutingNode->layerIdx < node->fixedLayers.high) {
        highestRoutingNode->children.push_back(std::make_shared<GRTreeNode>(node->fixedLayers.high, node->x, node->y));
    }
    return routingNode;
}
