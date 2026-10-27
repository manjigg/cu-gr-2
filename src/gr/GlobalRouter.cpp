// 文件功能：实现三阶段全局布线调度、拥塞线网重布、guide 补丁及布线质量统计。
// 阶段三先用 MazeRoute 改善二维拓扑，再交给 PatternRoute 完成实际金属层选择。
//
// 【零基础路线图】把芯片看作多层道路，线网看作要连通的几个站点。
// 第 1 轮先尝试直线/L 形等规则路径，速度快，给全部线网一个初步方案。
// 第 2 轮只处理发生资源溢出的线网，允许主干挪动，增加有限的绕行选择。
// 第 3 轮仍只处理有溢出的线网，用更自由的图搜索改变连接拓扑。
// 每轮结束都重新筛选，成功的线网暂时留在资源图里，失败的再继续优化。
// commitTree(tree,true) 是拆旧线；commitTree(tree) 是登记新线，必须成对理解。
// 本文件还负责把结果树变成 guide 区域，并扩大局部区域帮助详细布线接入。
#include "GlobalRouter.h"
#include "PatternRoute.h"
#include "MazeRoute.h"
#include <chrono>

GlobalRouter::GlobalRouter(const Design& design, const Parameters& params, bool importNets):
    gridGraph(design, params), parameters(params) {
    if (!importNets) return;
    // Instantiate the global routing netlist
    const vector<Net>& baseNets = design.getAllNets();
    // reserve 预留存储空间，避免反复分配；它不会创建那么多 GRNet。
    // 后面的 emplace_back 才逐个创建对象，列表 size 才会增加。
    nets.reserve(baseNets.size());
    for (const Net& baseNet : baseNets) {
        nets.emplace_back(baseNet, design, gridGraph);
    }
}

    // 这个文件有 337 行。我已经检查了其中的 GlobalRouter::route() 函数，发现它将全局布线划分成三个阶段。
    // 阶段	    实际代码中的名称	                                用大白话解释
    // 第一阶段	Pattern Routing（模式布线）	                        先为所有线网寻找比较规则的布线路径
    // 第二阶段	Pattern Routing with Detours（带绕行的模式布线）     如果某些线路拥挤，就尝试绕路
    // 第三阶段	Maze Routing（迷宫布线）	                        对仍有拥塞问题的线网，进行更灵活的路径搜索
    // 这三个阶段不是三个互不相关的算法，而是逐步处理布线问题，优先采用效率较高的方法，必要时再使用更灵活的方法。

void GlobalRouter::addRoutedNet(GRNet net, std::shared_ptr<SteinerTreeNode> topology) {
    if (net.getIndex() != nets.size()) throw std::invalid_argument("Non-contiguous routing net index");
    nets.push_back(std::move(net));
    PatternRoute router(nets.back(), gridGraph, parameters);
    if (topology) router.setSteinerTree(topology);
    else router.constructSteinerTree();
    router.constructRoutingDAG();
    router.run();
    gridGraph.commitTree(nets.back().getRoutingTree());
}

CapacitySyncStats GlobalRouter::synchronizeCapacities(const Design& design) {
    std::vector<GraphEdge> before;
    for (int layer = 0; layer < gridGraph.getNumLayers(); layer++) {
        for (int x = 0; x < gridGraph.getSize(0); x++) for (int y = 0; y < gridGraph.getSize(1); y++) {
            before.push_back(gridGraph.getEdge(layer, x, y));
        }
    }
    gridGraph.rebuildCapacities(design);
    // 独立新建最终几何地图作一致性基准；现有图必须保留全部已布需求。
    GridGraph fresh(design, parameters);
    CapacitySyncStats result;
    std::size_t index = 0;
    for (int layer = 0; layer < gridGraph.getNumLayers(); layer++) {
        for (int x = 0; x < gridGraph.getSize(0); x++) for (int y = 0; y < gridGraph.getSize(1); y++, index++) {
            auto edge = gridGraph.getEdge(layer, x, y);
            auto reference = fresh.getEdge(layer, x, y);
            if (edge.demand != before[index].demand) throw std::logic_error("Capacity synchronization changed committed routing demand");
            if (std::abs(edge.capacity - reference.capacity) > 1e-9) throw std::logic_error("Capacity map does not match final pad geometry");
            if (edge.capacity > before[index].capacity + 1e-9) throw std::logic_error("Adding fixed pads unexpectedly increased capacity");
            if (std::abs(edge.capacity - before[index].capacity) > 1e-9) result.changedEdges++;
            result.capacityReduction += before[index].capacity - edge.capacity;
        }
    }
    result.demandPreserved = true; result.matchesFreshGeometry = true;
    return result;
}

void GlobalRouter::route(bool initialRouting) {
    // 【局部变量】n1/n2/n3 是各阶段开始的待办网数量；t1/t2/t3 是各阶段耗时秒。
    // t 当前计时起点；netIndices 待处理网编号；netIndex 本轮编号；net 当前可修改 GRNet。
    // patternRoute 当前网的模式求解器；mazeRoute 当前网的迷宫求解器。
    // congestionView[direction][x][y] 阶段 2 开始时的拥塞真假快照；
    // wireCostView[direction][x][y] 阶段 3 的聚合边代价，每提交一网后更新受影响边。
    // grid 是采样间隔/偏移，不是资源图；tree 是 MazeRoute 返回的二维拓扑根。
    // 阶段末的 netIndices.clear 只清空待办，之后遍历全部 nets 再筛溢出网。
    // n1/n2/n3 记录各阶段处理数量；t1/t2/t3 记录耗时，单位秒。
    // auto t 保存当前时刻；后面用“结束时刻-开始时刻”求持续时间。
    int n1 = 0, n2 = 0, n3 = 0;
    double t1 = 0, t2 = 0, t3 = 0;
    
    auto t = std::chrono::high_resolution_clock::now();
    
    vector<int> netIndices;
    netIndices.reserve(nets.size());
    // netIndices 只是待处理编号清单，不复制整条线网。初始清单包含所有线网。
    for (const auto& net : nets) netIndices.push_back(net.getIndex());
    // 第一阶段处理全部线网，每完成一个就提交资源需求，后续线网据此避让。
    // Stage 1: Pattern routing
    n1 = netIndices.size();
    PatternRoute::readFluteLUT();
    log() << (initialRouting ? "stage 1: pattern routing" : "checking pre-routed subnet overflows") << std::endl;
    sortNetIndices(netIndices);
    if (initialRouting) for (const int netIndex : netIndices) {
        PatternRoute patternRoute(nets[netIndex], gridGraph, parameters);
        // ①确定要连接的二维拓扑；②展开候选路径；③按代价选路径和层。
        // 此处的 PatternRoute 是本轮当前线网的临时求解器，每轮都会重新创建。
        patternRoute.constructSteinerTree();
        patternRoute.constructRoutingDAG();
        patternRoute.run();
        gridGraph.commitTree(nets[netIndex].getRoutingTree());
    }
    
    // clear 清空的是“待处理编号”列表，nets 中线网和它们的已布树都还在。
    // 随后的筛选只把仍有溢出的编号重新装进清单。
    netIndices.clear();
    for (const auto& net : nets) {
        // checkOverflow(tree) 返回经过的溢出导线边数量；>0 意味着需要继续改善。
        if (gridGraph.checkOverflow(net.getRoutingTree()) > 0) {
            netIndices.push_back(net.getIndex());
        }
    }
    log() << netIndices.size() << " / " << nets.size() << " nets have overflows." << std::endl;
    logeol();
    
    t1 = std::chrono::duration<double>(std::chrono::high_resolution_clock::now() - t).count();
    t = std::chrono::high_resolution_clock::now();
    
    // Stage 2: Pattern routing with possible detours
    n2 = netIndices.size();
    if (netIndices.size() > 0) {
        log() << "stage 2: pattern routing with possible detours" << std::endl;
        GridGraphView<bool> congestionView; // (2d) direction -> x -> y -> has overflow?
        // 拥塞视图是阶段开始时的二维快照；各线网的代价仍读取实时三维资源。
        gridGraph.extractCongestionView(congestionView);
        // for (const int netIndex : netIndices) {
        //     GRNet& net = nets[netIndex];
        //     gridGraph.commitTree(net.getRoutingTree(), true);
        // }
        sortNetIndices(netIndices);
        for (const int netIndex : netIndices) {
            GRNet& net = nets[netIndex];
            // reverse=true 撤回旧路径的需求，重布完成后再提交新路径。
            gridGraph.commitTree(net.getRoutingTree(), true);
            PatternRoute patternRoute(net, gridGraph, parameters);
            patternRoute.constructSteinerTree();
            patternRoute.constructRoutingDAG();
            // 与第一轮的关键差别：添加绕行候选。并非强制使用绕行，旧规则候选仍可参与。
            patternRoute.constructDetours(congestionView); // KEY DIFFERENCE compared to stage 1
            patternRoute.run();
            gridGraph.commitTree(net.getRoutingTree());
        }
        
        netIndices.clear();
        for (const auto& net : nets) {
            if (gridGraph.checkOverflow(net.getRoutingTree()) > 0) {
                netIndices.push_back(net.getIndex());
            }
        }
        log() << netIndices.size() << " / " << nets.size() << " nets have overflows." << std::endl;
        logeol();
    }
    
    t2 = std::chrono::duration<double>(std::chrono::high_resolution_clock::now() - t).count();
    t = std::chrono::high_resolution_clock::now();
    
    // Stage 3: maze routing on sparsified routing graph
    n3 = netIndices.size();
    if (netIndices.size() > 0) {
        log() << "stage 3: maze routing on sparsified routing graph" << std::endl;
        for (const int netIndex : netIndices) {
            GRNet& net = nets[netIndex];
            gridGraph.commitTree(net.getRoutingTree(), true);
        }
        // 本阶段先整体撤回待修复线网，再建立按水平/垂直方向聚合的代价视图。
        GridGraphView<CostT> wireCostView;
        gridGraph.extractWireCostView(wireCostView);
        sortNetIndices(netIndices);
        // 每隔约 10 格取一条候选搜索线；引脚坐标会另外加入，不会因采样丢掉引脚。
        // 这两个 10 是采样间隔，不是 10×10 的整个芯片大小。
        SparseGrid grid(10, 10, 0, 0);
        for (const int netIndex : netIndices) {
            GRNet& net = nets[netIndex];
            // gridGraph.commitTree(net.getRoutingTree(), true);
            // gridGraph.updateWireCostView(wireCostView, net.getRoutingTree());
            MazeRoute mazeRoute(net, gridGraph, parameters);
            mazeRoute.constructSparsifiedGraph(wireCostView, grid);
            mazeRoute.run();
            std::shared_ptr<SteinerTreeNode> tree = mazeRoute.getSteinerTree();
            // assert 检查程序内部预期：这里应该取得有效拓扑树。
            // nullptr 是空指针；与它不相等表示当前确实有一个树对象。
            assert(tree != nullptr);
            
            // 迷宫搜索只给出拓扑；复用 DAG 动态规划恢复带真实层索引的三维布线树。
            PatternRoute patternRoute(net, gridGraph, parameters);
            patternRoute.setSteinerTree(tree);
            patternRoute.constructRoutingDAG();
            patternRoute.run();
            
            gridGraph.commitTree(net.getRoutingTree());
            gridGraph.updateWireCostView(wireCostView, net.getRoutingTree());
            // 下一个线网改变采样偏移，避免所有线网只能用完全相同的稀疏道路。
            grid.step();
        }
        netIndices.clear();
        for (const auto& net : nets) {
            if (gridGraph.checkOverflow(net.getRoutingTree()) > 0) {
                netIndices.push_back(net.getIndex());
            }
        }
        log() << netIndices.size() << " / " << nets.size() << " nets have overflows." << std::endl;
        logeol();
    }
    
    t3 = std::chrono::duration<double>(std::chrono::high_resolution_clock::now() - t).count();
    t = std::chrono::high_resolution_clock::now();
    
    // std::cout << "iteration statistics " 
    //     << n1 << " " << std::setprecision(3) << std::fixed << t1 << " " 
    //     << n2 << " " << std::setprecision(3) << std::fixed << t2 << " " 
    //     << n3 << " " << std::setprecision(3) << std::fixed << t3 << std::endl;
    
    printStatistics();
    if (parameters.write_heatmap) gridGraph.write();
}

// 按网格包围盒半周长升序处理，让范围较小的线网优先选择路径。
void GlobalRouter::sortNetIndices(vector<int>& netIndices) const {
    // halfParameters[网编号]：该网候选包围盒宽+高，单位是格子索引尺度。
    // lhs/rhs 为比较的两个网编号，不是两个线长；sort 调用此比较器重排 netIndices。
    vector<int> halfParameters(nets.size());
    for (int netIndex : netIndices) {
        auto& net = nets[netIndex];
        halfParameters[netIndex] = net.getBoundingBox().hp();
    }
    // sort 的最后一个参数是“谁排前面”的小函数，写作 lambda。
    // [&] 借用周围变量，lhs/rhs 是比较的两个线网编号；返回 true 表示 lhs 排前面。
    sort(netIndices.begin(), netIndices.end(), [&](int lhs, int rhs) {
        return halfParameters[lhs] < halfParameters[rhs];
    });
}

// 基础路径 guide 加两类补丁：低层引脚跨层接入，以及拥塞导线附近的相邻层补充。
void GlobalRouter::getGuides(const GRNet& net, vector<std::pair<int, utils::BoxT<int>>>& guides) {
    // net 输入当前网，routingTree 其三维树根，node/child 当前父/子节点。
    // guides 输出“真实层编号,格子索引矩形”，尚未转 DBU；maxLayerIndex 跨层高端。
    // getSpareResource 小函数输入 point；resource 当前最小余额；direction 该层首选方向。
    // lower 是沿走线轴减一的临时点，用它的坐标查询低端相邻边。
    // gpts 为单引脚候选列表，gpt 当前候选；padding 扩到周围多少格。
    // wire_patch_threshold 当前局部阈值；l/h 线段走线轴两端，r 固定的另一轴，c 当前格位置。
    // point 当前补丁位置，patched 本格是否补成功，layerIndex 要尝试的相邻真实层。
    auto& routingTree = net.getRoutingTree();
    if (!routingTree) return;
    if (routingTree->children.empty()) {
        guides.emplace_back(routingTree->layerIdx, utils::BoxT<int>(routingTree->x, routingTree->y));
    }
    // 0. Basic guides
    // 给遍历器一段操作：每遇到一个节点，就把它与孩子之间的连接转成矩形。
    // -> 通过 shared_ptr 访问节点；node->children 是这个节点的孩子列表。
    GRTreeNode::preorder(routingTree, [&](std::shared_ptr<GRTreeNode> node) {
        for (const auto& child : node->children) {
            if (node->layerIdx == child->layerIdx) {
                guides.emplace_back(
                    node->layerIdx, utils::BoxT<int>(
                        min(node->x, child->x), min(node->y, child->y),
                        max(node->x, child->x), max(node->y, child->y)
                    )
                );
            } else {
                int maxLayerIndex = max(node->layerIdx, child->layerIdx);
                for (int layerIdx = min(node->layerIdx, child->layerIdx); layerIdx <= maxLayerIndex; layerIdx++) {
                    guides.emplace_back(layerIdx, utils::BoxT<int>(node->x, node->y));
                }
            }
        }
    });
    
    
    // 定义只在当前函数使用的 lambda，查询一个位置沿首选方向附近的资源余额。
    // 取较小余额让判断更保守；不在这里对图增加/减少需求。
    auto getSpareResource = [&] (const GRPoint& point) {
        double resource = std::numeric_limits<double>::max();
        unsigned direction = gridGraph.getLayerDirection(point.layerIdx);
        if (point[direction] + 1 < gridGraph.getSize(direction)) {
            resource = min(resource, gridGraph.getEdge(point.layerIdx, point.x, point.y).getResource());
        }
        if (point[direction] > 0) {
            GRPoint lower = point;
            lower[direction] -= 1;
            resource = min(resource, gridGraph.getEdge(lower.layerIdx, lower.x, lower.y).getResource());
        }
        return resource;
    };
    
    // 如果引脚在最低布线层以下，需给详细布线器提供往上接入的多层区域。
    // 余额不足时 padding 扩大 x/y 周围的格子范围，max/min 防止越过芯片边缘。
    // 1. Pin access patches
    assert(parameters.min_routing_layer + 1 < gridGraph.getNumLayers());
    for (auto& gpts : net.getPinAccessPoints()) {
        for (auto& gpt : gpts) {
            if (gpt.layerIdx < parameters.min_routing_layer) {
                int padding = 0;
                if (getSpareResource({parameters.min_routing_layer, gpt.x, gpt.y}) < parameters.pin_patch_threshold) {
                    padding = parameters.pin_patch_padding;
                }
                for (int layerIdx = gpt.layerIdx; layerIdx <= parameters.min_routing_layer + 1; layerIdx++) {
                    guides.emplace_back(layerIdx, utils::BoxT<int>(
                        max(gpt.x - padding, 0),
                        max(gpt.y - padding, 0),
                        min(gpt.x + padding, (int)gridGraph.getSize(0) - 1),
                        min(gpt.y + padding, (int)gridGraph.getSize(1) - 1)
                    ));
                    areaOfPinPatches += (guides.back().second.x.range() + 1) * (guides.back().second.y.range() + 1);
                }
            }
        }
    }
    
    // 导线经过资源紧张的位置时，尝试在相邻层增加 guide 格子。
    // 这只是允许详细布线多一个选择，不是在此处改写原 routingTree。
    // 2. Wire segment patches
    GRTreeNode::preorder(routingTree, [&](std::shared_ptr<GRTreeNode> node) {
        for (const auto& child : node->children) {
            if (node->layerIdx == child->layerIdx) {
                double wire_patch_threshold = parameters.wire_patch_threshold;
                unsigned direction = gridGraph.getLayerDirection(node->layerIdx);
                int l = min((*node)[direction], (*child)[direction]);
                int h = max((*node)[direction], (*child)[direction]);
                int r = (*node)[1 - direction];
                for (int c = l; c <= h; c++) {
                    bool patched = false;
                    GRPoint point = (direction == MetalLayer::H ? GRPoint(node->layerIdx, c, r) : GRPoint(node->layerIdx, r, c));
                    if (getSpareResource(point) < wire_patch_threshold) {
                        for (int layerIndex = node->layerIdx - 1; layerIndex <= node->layerIdx + 1; layerIndex += 2) {
                            if (layerIndex < parameters.min_routing_layer || layerIndex >= gridGraph.getNumLayers()) continue;
                            if (getSpareResource({layerIndex, point.x, point.y}) >= 1.0) {
                                guides.emplace_back(layerIndex, utils::BoxT<int>(point.x, point.y));
                                areaOfWirePatches += 1;
                                patched = true;
                            }
                        }
                    } 
                    if (patched) {
                        wire_patch_threshold = parameters.wire_patch_threshold;
                    } else {
                        wire_patch_threshold *= parameters.wire_patch_inflation_rate;
                    }
                }
            }
        }
    });
}

// 分别统计线长、过孔数量和线容量溢出；溢出统计只计导线，资源余额还包含过孔需求。
void GlobalRouter::printStatistics() const {
    // wireLength 结果树普通线的 DBU 累计长度；viaCount 相邻层过孔总数。
    // wireUsage[layer][x][y] 只记录普通线经过次数，区别于含过孔需求的 graphEdges.demand。
    // direction 当前层 H/V；l/h/r/c 走线轴低/高端、固定轴、当前边位置；x/y 对应实际格子下标。
    // overflow 普通线使用超过非负容量的累计量；usage 当前普通线次数；capacity 截断后的容量。
    // minResource 图上最小 capacity-demand；bottleneck 这个最小余额位置，初始 (-1,-1,-1)。
    log() << "routing statistics" << std::endl;
    loghline();

    // wire length and via count
    uint64_t wireLength = 0;
    int viaCount = 0;
    vector<vector<vector<int>>> wireUsage;
    wireUsage.assign(
        gridGraph.getNumLayers(), vector<vector<int>>(gridGraph.getSize(0), vector<int>(gridGraph.getSize(1), 0))
    );
    for (const auto& net : nets) {
        GRTreeNode::preorder(net.getRoutingTree(), [&] (std::shared_ptr<GRTreeNode> node) {
            for (const auto& child : node->children) {
                if (node->layerIdx == child->layerIdx) {
                    unsigned direction = gridGraph.getLayerDirection(node->layerIdx);
                    int l = min((*node)[direction], (*child)[direction]);
                    int h = max((*node)[direction], (*child)[direction]);
                    int r = (*node)[1 - direction];
                    for (int c = l; c < h; c++) {
                        wireLength += gridGraph.getEdgeLength(direction, c);
                        int x = direction == MetalLayer::H ? c : r;
                        int y = direction == MetalLayer::H ? r : c;
                        wireUsage[node->layerIdx][x][y] += 1;
                    }
                } else {
                    viaCount += abs(node->layerIdx - child->layerIdx);
                }
            }
        });
    }
    
    // resource
    CapacityT overflow = 0;

    CapacityT minResource = std::numeric_limits<CapacityT>::max();
    GRPoint bottleneck(-1, -1, -1);
    for (int layerIndex = parameters.min_routing_layer; layerIndex < gridGraph.getNumLayers(); layerIndex++) {
        unsigned direction = gridGraph.getLayerDirection(layerIndex);
        for (int x = 0; x < gridGraph.getSize(0) - 1 + direction; x++) {
            for (int y = 0; y < gridGraph.getSize(1) - direction; y++) {
                CapacityT resource = gridGraph.getEdge(layerIndex, x, y).getResource();
                if (resource < minResource) {
                    minResource = resource;
                    bottleneck = {layerIndex, x, y};
                }
                CapacityT usage = wireUsage[layerIndex][x][y];
                CapacityT capacity = max(gridGraph.getEdge(layerIndex, x, y).capacity, 0.0);
                if (usage > 0.0 && usage > capacity) {
                    overflow += usage - capacity;
                }
            }
        }
    }
    
    log() << "wire length (metric):  " << wireLength / gridGraph.getM2Pitch() << std::endl;
    log() << "total via count:       " << viaCount << std::endl;
    log() << "total wire overflow:   " << (int)overflow << std::endl;
    logeol();

    log() << "min resource: " << minResource << std::endl;
    log() << "bottleneck:   " << bottleneck << std::endl;

    logeol();
}

// guide 矩形使用网格边界的 DBU 坐标；high+1 将包含的最后一个网格扩展到其外边界。
void GlobalRouter::write(std::string guide_file) {
    // guide_file 输出路径，空串用 parameters.out_file；ss 内存文本缓存；fout 磁盘输出流。
    // net 当前线网；guides 当前网区域列表；guide 单区域，first 层编号、second 索引矩形。
    // 每条记录用 low 边界、high+1 外边界转换坐标，再用 layerName 输出输入的工艺层名。
    log() << "generating route guides..." << std::endl;
    if (guide_file == "") guide_file = parameters.out_file;
    
    areaOfPinPatches = 0;
    areaOfWirePatches = 0;
    // stringstream 像内存中的文本文件：先拼完整文本，之后一次写出到磁盘。
    std::stringstream ss;
    for (const GRNet& net : nets) {
        vector<std::pair<int, utils::BoxT<int>>> guides;
        getGuides(net, guides);
        
        ss << net.getName() << std::endl;
        ss << "(" << std::endl;
        for (const auto& guide : guides) {
            ss << gridGraph.getGridline(0, guide.second.x.low) << " "
                 << gridGraph.getGridline(1, guide.second.y.low) << " "
                 << gridGraph.getGridline(0, guide.second.x.high + 1) << " "
                 << gridGraph.getGridline(1, guide.second.y.high + 1) << " "
                 << gridGraph.getLayerName(guide.first) << std::endl;
        }
        ss << ")" << std::endl;
    }
    log() << "total area of pin access patches: " << areaOfPinPatches << std::endl;
    log() << "total area of wire segment patches: " << areaOfWirePatches << std::endl;
    log() << std::endl;
    log() << "writing output..." << std::endl;
    std::ofstream fout(guide_file);
    if (!fout) throw std::runtime_error("Cannot open guide output: " + guide_file);
    fout << ss.str();
    fout.close();
    log() << "finished writing output..." << std::endl;
}
