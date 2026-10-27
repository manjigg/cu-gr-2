// 文件功能：建立 GCell 资源图，按轨道及障碍估计容量，实现拥塞代价、需求更新和二维投影。
// 这是路由算法共享的资源模型：PatternRoute 查询真实层代价，MazeRoute 使用方向聚合视图。
//
// 【阅读地图】构造函数建图；rangeSearch* 做坐标查询；get*Cost 做评分；
// selectAccessPoints 选引脚入口；commit* 更新账本；checkOverflow 检查是否挤满；
// extract*/update* 构造/刷新二维视图；write 导出余额给画图工具。
// 同层导线沿本层首选方向走，拐弯通常需要换到首选方向不同的层。
// capacity 先来自轨道数，再因障碍减小；demand 来自当前已提交的路线。
// 算法比较路线时只读这两个量，选定之后才提交，避免把“比较候选”当成占用。
// 导线和过孔都可改变 demand，但 checkOverflow(tree) 只沿导线边检查。
#include "GridGraph.h"
#include "GRNet.h"

GridGraph::GridGraph(const Design& design, const Parameters& params): libDBU(design.getLibDBU()), parameters(params) {
    // 【建图变量】design 输入精简数据库，params 参数表；dimension 当前 x/y 轴，gridIndex 格子下标。
    // layerIndex 真实层下标，layer 其工艺规则，direction H/V；gridTracks[layer][排编号] 轨道数量。
    // nGrids 轨道排数，nEdges 走线轴相邻格子间边数；locRange 某排的 DBU 范围；
    // trackRange 该范围内轨道编号区间；nTracks 当前排容量起点。
    // obstacles[layer][i] 所有障碍；obs 当前原矩形；width 矩形短边；spacing 扩张距离 DBU；
    // margin 扩张向量（只沿垂直走线轴），obsBox 扩大后障碍，obstacle 矩形+轨道范围共享记录。
    // obstaclesInGrid[排] 每排相关障碍；gridRange 障碍覆盖排编号；gridTrackRange 当前排轨道区间；
    // obstaclesAtEdge[边] 当前排逐边障碍列表；gridline 当前跨格边界 DBU；edgeInterval 两中心区间。
    // usableIntervals[tIdx] 当前排第 tIdx 轨道仍可用的物理长度区间；
    // affectedTrackRange 障碍与当前排相交轨道区间；trackIndex 全层轨道编号；tIdx 排内相对编号。
    // capacity 各轨有效比例之和；minEdgeLength 本层走线轴最短相邻中心间距。
    gridlines = design.getGridlines();
    nLayers = design.getNumLayers();
    // 边界线比格子多一条：3 条竖边界和 4 条横边界划出 2×3 个格子。
    xSize = gridlines[0].size() - 1;
    ySize = gridlines[1].size() - 1;
    
    gridCenters.resize(2);
    for (unsigned dimension = 0; dimension <= 1; dimension++) {
        gridCenters[dimension].resize(gridlines[dimension].size() - 1);
        for (int gridIndex = 0; gridIndex < gridlines[dimension].size() - 1; gridIndex++) {
            gridCenters[dimension][gridIndex] = 
                (gridlines[dimension][gridIndex] + gridlines[dimension][gridIndex + 1]) / 2;
            // 两条边界中点是格子中心；相邻中心距离才是导线网格边的物理长度。
        }
    }
    
    m2_pitch = design.getLayer(1).getPitch();

    layerNames.resize(nLayers);
    layerDirections.resize(nLayers);
    layerMinLengths.resize(nLayers);
    for (int layerIndex = 0; layerIndex < nLayers; layerIndex++) {
        const auto& layer = design.getLayer(layerIndex);
        layerNames[layerIndex] = layer.getName();
        layerDirections[layerIndex] = layer.getDirection();
        layerMinLengths[layerIndex] = layer.getMinLength();
    }
    
    unit_length_wire_cost = design.getUnitLengthWireCost();
    unit_via_cost = design.getUnitViaCost();
    unit_length_short_costs.resize(nLayers);
    for (int layerIndex = 0; layerIndex < nLayers; layerIndex++) {
        unit_length_short_costs[layerIndex] = design.getUnitLengthShortCost(layerIndex);
    }
    
    // 每层只建立首选方向边；跨 GCell 的边以其较低坐标端点作为数组索引。
    // Init grid graph edges
    graphEdges.assign(nLayers, vector<vector<GraphEdge>>(xSize, vector<GraphEdge>(ySize)));
    rebuildCapacities(design);
}

void GridGraph::rebuildCapacities(const Design& design) {
    if (design.getNumLayers() != nLayers || design.getGridlines() != gridlines) {
        throw std::invalid_argument("Cannot change the layer/grid topology while rebuilding capacities");
    }
    // 与初次建图共用同一套轨道/间距/障碍区间计算，不清空 graphEdges.demand。
    vector<vector<int>> gridTracks(nLayers);
    for (int layerIndex = 0; layerIndex < nLayers; layerIndex++) {
        const MetalLayer& layer = design.getLayer(layerIndex);
        const unsigned direction = layer.getDirection();
        
        const int nGrids = gridlines[1 - direction].size() - 1;
        gridTracks[layerIndex].resize(nGrids);
        for (size_t gridIndex = 0; gridIndex < nGrids; gridIndex++) {
            utils::IntervalT<DBU> locRange(gridlines[1 - direction][gridIndex], gridlines[1 - direction][gridIndex + 1]);
            auto trackRange = layer.rangeSearchTracks(locRange);
            if (trackRange.IsValid()) {
                gridTracks[layerIndex][gridIndex] = trackRange.range() + 1;
                // 边界轨道不能同时算到相邻两排，否则可用资源被重复计算。
                // exclude the track on the higher gridline
                if (gridIndex != nGrids - 1 && layer.getTrackLocation(trackRange.high) == locRange.high) {
                    gridTracks[layerIndex][gridIndex]--;
                }
            } else {
                gridTracks[layerIndex][gridIndex] = 0;
            }
        }
        
        // Initialize edges' capacity to the number of tracks
        if (direction == MetalLayer::V) {
            for (size_t x = 0; x < xSize; x++) {
                CapacityT nTracks = gridTracks[layerIndex][x];
                for (size_t y = 0; y + 1 < ySize; y++) {
                    graphEdges[layerIndex][x][y].capacity = nTracks;
                }
            }
        } else {
            for (size_t y = 0; y < ySize; y++) {
                CapacityT nTracks = gridTracks[layerIndex][y];
                for (size_t x = 0; x + 1 < xSize; x++) {
                    graphEdges[layerIndex][x][y].capacity = nTracks;
                }
            }
        }
    }

    // 按间距规则扩张障碍，再按各轨道仍可用的长度比例累加边容量。
    // 因此 capacity 不只是整数轨道计数，也能表达局部阻塞。
    // Deduct obstacles usage for layers EXCEPT Metal 1
    vector<vector<utils::BoxT<DBU>>> obstacles(nLayers);
    design.getAllObstacles(obstacles, true);
    for (int layerIndex = 1; layerIndex < nLayers; layerIndex++) {
        const MetalLayer& layer = design.getLayer(layerIndex);
        unsigned direction = layer.getDirection();
        const int nGrids = gridlines[1 - direction].size() - 1;
        const int nEdges = gridlines[direction].size() - 2;
        DBU minEdgeLength = std::numeric_limits<DBU>::max();
        for (int edgeIndex = 0; edgeIndex < nEdges; edgeIndex++) {
            minEdgeLength = min(minEdgeLength, gridCenters[direction][edgeIndex + 1] - gridCenters[direction][edgeIndex]);
        }
        vector<vector<std::shared_ptr<std::pair<utils::BoxT<DBU>, utils::IntervalT<int>>>>> obstaclesInGrid(nGrids); // obstacle indices sorted in track grids
        // Sort obstacles in track grids
        for (auto& obs : obstacles[layerIndex]) {
            DBU width = min(obs.x.range(), obs.y.range());
            // 新导线的中心必须与已有金属保持间距，还要留出新导线半个线宽。
            // 所以先扩张障碍，扩张后覆盖到的轨道中心才判为受影响。
            DBU spacing = layer.getParallelSpacing(width, min(minEdgeLength, obs[direction].range())) + layer.getWidth() / 2 - 1;
            utils::PointT<DBU> margin(0, 0);
            margin[1 - direction] = spacing;
            utils::BoxT<DBU> obsBox(
                obs.x.low  - margin.x, obs.y.low  - margin.y, 
                obs.x.high + margin.x, obs.y.high + margin.y
            ); // enlarged obstacle box
            utils::IntervalT<int> trackRange = layer.rangeSearchTracks(obsBox[1 - direction]);
            std::shared_ptr<std::pair<utils::BoxT<DBU>, utils::IntervalT<int>>> obstacle = 
                std::make_shared<std::pair<utils::BoxT<DBU>, utils::IntervalT<int>>>(obsBox, trackRange);
            // Get grid range
            utils::IntervalT<int> gridRange = rangeSearchRows(1 - direction, obsBox[1 - direction]);
            for (int gridIndex = gridRange.low; gridIndex <= gridRange.high; gridIndex++) {
                obstaclesInGrid[gridIndex].push_back(obstacle);
            }
        }
        // Handle each track grid
        utils::IntervalT<int> gridTrackRange;
        for (int gridIndex = 0; gridIndex < nGrids; gridIndex++) {
            if (gridIndex == 0) {
                gridTrackRange.low = 0;
                gridTrackRange.high = gridTracks[layerIndex][gridIndex] - 1;
            } else {
                gridTrackRange.low = gridTrackRange.high + 1;
                gridTrackRange.high += gridTracks[layerIndex][gridIndex];
            }
            if (!gridTrackRange.IsValid()) continue;
            if (obstaclesInGrid[gridIndex].size() == 0) continue;
            vector<vector<std::shared_ptr<std::pair<utils::BoxT<DBU>, utils::IntervalT<int>>>>> obstaclesAtEdge(nEdges);
            for (auto& obstacle : obstaclesInGrid[gridIndex]) {
                utils::IntervalT<int> gridlineRange = rangeSearchGridlines(direction, obstacle->first[direction]);
                utils::IntervalT<int> edgeRange(max(gridlineRange.low - 2, 0), min(gridlineRange.high, nEdges - 1));
                for (int edgeIndex = edgeRange.low; edgeIndex <= edgeRange.high; edgeIndex++) {
                    obstaclesAtEdge[edgeIndex].emplace_back(obstacle);
                }
            }
            for (int edgeIndex = 0; edgeIndex < nEdges; edgeIndex++) {
                if (obstaclesAtEdge[edgeIndex].size() == 0) continue;
                DBU gridline = gridlines[direction][edgeIndex + 1];
                utils::IntervalT<DBU> edgeInterval(gridCenters[direction][edgeIndex], gridCenters[direction][edgeIndex + 1]);
                // Update cpacity
                // 每条轨道先假设整段可用，遇障碍后缩短对应有效区间。
                // 障碍跨过格子分界则这条跨格边被完全阻断。
                vector<utils::IntervalT<DBU>> usableIntervals(
                    gridTrackRange.range() + 1, edgeInterval
                );
                for (auto& obstacle : obstaclesAtEdge[edgeIndex]) {
                    utils::IntervalT<int> affectedTrackRange = gridTrackRange.IntersectWith(obstacle->second);
                    if (!affectedTrackRange.IsValid()) continue;
                    for (int trackIndex = affectedTrackRange.low; trackIndex <= affectedTrackRange.high; trackIndex++) {
                        int tIdx = trackIndex - gridTrackRange.low;
                        if (obstacle->first[direction].low <= gridline && obstacle->first[direction].high >= gridline) {
                            // Completely blocked 
                            usableIntervals[tIdx] = {gridline, gridline};
                        } else if (obstacle->first[direction].high < gridline) {
                            usableIntervals[tIdx].low = max(usableIntervals[tIdx].low, obstacle->first[direction].high);
                        } else if (obstacle->first[direction].low > gridline) {
                            usableIntervals[tIdx].high = min(usableIntervals[tIdx].high, obstacle->first[direction].low);
                        }
                        // if (obstacle->first[direction].HasStrictIntersectWith(usableIntervals[tIdx])) {
                        //     usableIntervals[tIdx] = {gridline, gridline};
                        // }
                    }
                }
                CapacityT capacity = 0;
                for (int tIdx = 0; tIdx < usableIntervals.size(); tIdx++) {
                    // 例：三条轨道分别剩 100%、50%、0% 长度，容量约为 1.5。
                    // 这是全局资源的近似估计，不表示实际可以制造半条完整导线。
                    capacity += (CapacityT)usableIntervals[tIdx].range() / edgeInterval.range();
                }
                if (direction == MetalLayer::V) {
                    graphEdges[layerIndex][gridIndex][edgeIndex].capacity = capacity;
                } else {
                    graphEdges[layerIndex][edgeIndex][gridIndex].capacity = capacity;
                }
            }
        }
    }
}

utils::IntervalT<int> GridGraph::rangeSearchGridlines(const unsigned dimension, const utils::IntervalT<DBU>& locInterval) const {
    // dimension 查询轴 0=x/1=y；locInterval 输入 DBU 范围；range 返回边界线下标范围。
    // lower_bound 在排好序的边界里找第一个不小于目标值的位置。
    // iterator - begin() 将位置换成整数下标；再修正右端到真正落在范围内的边界。
    utils::IntervalT<int> range;
    range.low = lower_bound(gridlines[dimension].begin(), gridlines[dimension].end(), locInterval.low) - gridlines[dimension].begin();
    range.high = lower_bound(gridlines[dimension].begin(), gridlines[dimension].end(), locInterval.high) - gridlines[dimension].begin();
    if (range.high >= gridlines[dimension].size()) {
        range.high = gridlines[dimension].size() - 1;
    } else if (gridlines[dimension][range.high] > locInterval.high) {
        range.high -= 1;
    }
    return range;
}
  
utils::IntervalT<int> GridGraph::rangeSearchRows(const unsigned dimension, const utils::IntervalT<DBU>& locInterval) const {
    // lineRange 是已找到的边界线范围，返回值改成相交的格子排/列下标，不是 DBU。
    // 输入是物理区间，输出是与之相交的格子编号区间。
    // 若高端正落在边界上，使用左边格子，避免把仅接触边界的下一格也算进去。
    const auto& lineRange = rangeSearchGridlines(dimension, locInterval);
    if (lineRange.low >= gridlines[dimension].size() || lineRange.high < 0) {
        throw std::out_of_range("Physical geometry lies outside the routing grid");
    }
    return {
        gridlines[dimension][lineRange.low] == locInterval.low ? lineRange.low : max(lineRange.low - 1, 0),
        gridlines[dimension][lineRange.high] == locInterval.high ? lineRange.high - 1 : min(lineRange.high, static_cast<int>(getSize(dimension)) - 1)
    };
}

utils::BoxT<DBU> GridGraph::getCellBox(utils::PointT<int> point) const {
    return {
        getGridline(0, point.x), getGridline(1, point.y),
        getGridline(0, point.x + 1), getGridline(1, point.y + 1)
    };
}

utils::BoxT<int> GridGraph::rangeSearchCells(const utils::BoxT<DBU>& box) const {
    return {rangeSearchRows(0, box[0]), rangeSearchRows(1, box[1])};
}

DBU GridGraph::getEdgeLength(unsigned direction, unsigned edgeIndex) const {
    return gridCenters[direction][edgeIndex + 1] - gridCenters[direction][edgeIndex];
}

// 剩余容量越小，惩罚越接近 1；剩余容量充足时惩罚趋近 0。
inline double GridGraph::logistic(const CapacityT& input, const double slope) const {
    return 1.0 / (1.0 + exp(input * slope));
}

// 单边代价 = 需求线长代价 + 随拥塞风险加权的短路代价；容量不足一轨时使用最大惩罚。
CostT GridGraph::getWireCost(const int layerIndex, const utils::PointT<int> lower, const CapacityT demand) const {
    // direction 当前层走线轴；edgeLength 单边 DBU 长度；demandLength=需求×长度（DBU 整数）。
    // edge 当前资源记录；cost 累计分；lower 单边低端位置，demand 本候选的预计需求默认 1。
    // lower 是边的低坐标端点，demand 默认 1 即估计一条普通线。
    // 获取长度后乘需求得 demandLength；现有代码用 DBU 存它，包含类型转换取整。
    // 因此解释公式时可看成乘法，但逐数值复现时还应保留这个整数转换。
    unsigned direction = layerDirections[layerIndex];
    DBU edgeLength = getEdgeLength(direction, lower[direction]);
    DBU demandLength = demand * edgeLength;
    const auto& edge = graphEdges[layerIndex][lower.x][lower.y];
    CostT cost = demandLength * unit_length_wire_cost;
    cost += demandLength * unit_length_short_costs[layerIndex] * (
        edge.capacity < 1.0 ? 1.0 : logistic(edge.capacity - edge.demand, parameters.cost_logistic_slope)
    );
    return cost;
}

CostT GridGraph::getWireCost(const int layerIndex, const utils::PointT<int> u, const utils::PointT<int> v) const {
    // 这是同名函数的另一个版本（重载）：传两个端点，累加所有经过边的代价。
    // 不只按两端直线长度评分，还要考虑途中每条边已有的拥塞状态。
    unsigned direction = layerDirections[layerIndex];
    assert(u[1 - direction] == v[1 - direction]);
    CostT cost = 0;
    if (direction == MetalLayer::H) {
        int l = min(u.x, v.x), h = max(u.x, v.x);
        for (int x = l; x < h; x++) cost += getWireCost(layerIndex, {x, u.y});
    } else {
        int l = min(u.y, v.y), h = max(u.y, v.y);
        for (int y = l; y < h; y++) cost += getWireCost(layerIndex, {u.x, y});
    }
    return cost;
}

// 过孔除了固定代价，还估算上下层为满足最小面积所需的额外线需求。
CostT GridGraph::getViaCost(const int layerIndex, const utils::PointT<int> loc) const {
    // loc 过孔格子位置；l 逐个枚举上下两真实层；lowerLoc 沿该层方向减一的相邻低端。
    // lowerEdgeLength/higherEdgeLength 左/右或下/上边长，不存在的边记 0。
    // demand 满足最小面积估计的附加需求，分摊到可用邻边；cost 基础过孔分+两层附加线分。
    // 只算 layerIndex 与 layerIndex+1 之间的一个过孔。
    // 较长过孔堆叠在其他代码中展开成多个相邻层连接，再逐个调用本函数。
    assert(layerIndex + 1 < nLayers);
    CostT cost = unit_via_cost;
    // Estimated wire cost to satisfy min-area
    for (int l = layerIndex; l <= layerIndex + 1; l++) {
        unsigned direction = layerDirections[l];
        utils::PointT<int> lowerLoc = loc;
        lowerLoc[direction] -= 1;
        DBU lowerEdgeLength = loc[direction] > 0 ? getEdgeLength(direction, lowerLoc[direction]) : 0;
        DBU higherEdgeLength = loc[direction] < getSize(direction) - 1 ? getEdgeLength(direction, loc[direction]) : 0;
        CapacityT demand = (CapacityT) layerMinLengths[l] / (lowerEdgeLength + higherEdgeLength) * parameters.via_multiplier;
        if (lowerEdgeLength > 0) cost += getWireCost(l, lowerLoc, demand);
        if (higherEdgeLength > 0) cost += getWireCost(l, loc, demand);
    }
    return cost;
}

void GridGraph::selectAccessPoints(GRNet& net, robin_hood::unordered_map<uint64_t, std::pair<utils::PointT<int>, utils::IntervalT<int>>>& selectedAccessPoints) const {
    // boundingBox 网候选索引包围盒，netCenter 盒中心；accessPoints 单引脚候选表。
    // bestAccessDist.first 最多可接入边数，second 最短到中心距离；bestIndex 当前最佳候选下标。
    // point 当前候选，accessibility 按 capacity>=1 计可接入边数，distance 曼哈顿格子距离。
    // selectedPoint 选中二维位置，hash 二维位置编码，fixedLayerInterval 同位置候选真实层范围。
    // selectedAccessPoints[hash].first 坐标，second 必连层区间；清空输出后逐引脚追加/合并。
    selectedAccessPoints.clear();
    // 先选择可接入边最多的位置，再以距线网中心的曼哈顿距离打破平局。
    // 同一二维位置上的候选层合并为 fixedLayers，约束后续过孔堆叠范围。
    // cell hash (2d) -> access point, fixed layer interval
    selectedAccessPoints.reserve(net.getNumPins());
    const auto& boundingBox = net.getBoundingBox();
    utils::PointT<int> netCenter(boundingBox.cx(), boundingBox.cy());
    for (const auto& accessPoints : net.getPinAccessPoints()) {
        // pair 把两项候选指标装成一对：first 是可接入边数，second 是到中心距离。
        // max() 是整数类型可表示的最大值，初始化为“还没有找到更近的点”。
        std::pair<int, int> bestAccessDist = {0, std::numeric_limits<int>::max()};
        int bestIndex = -1;
        for (int index = 0; index < accessPoints.size(); index++) {
            const auto& point = accessPoints[index];
            int accessibility = 0;
            if (point.layerIdx >= parameters.min_routing_layer) {
                unsigned direction = getLayerDirection(point.layerIdx);
                accessibility += getEdge(point.layerIdx, point.x, point.y).capacity >= 1;
                if (point[direction] > 0) {
                    auto lower = point;
                    lower[direction] -= 1;
                    accessibility += getEdge(lower.layerIdx, lower.x, lower.y).capacity >= 1;
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
        }
        const utils::PointT<int> selectedPoint = accessPoints[bestIndex];
        const uint64_t hash = hashCell(selectedPoint.x, selectedPoint.y);
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
        // 向上扩展两层为轨道切换留余地，但不越过最后一层。
        fixedLayers.high = min(fixedLayers.high + 2, (int)getNumLayers() - 1);
    }
}

void GridGraph::commit(const int layerIndex, const utils::PointT<int> lower, const CapacityT demand) {
    // += 是把新值加到原值：正数登记占用，负数撤销占用，不改变容量。
    graphEdges[layerIndex][lower.x][lower.y].demand += demand;
}

void GridGraph::commitWire(const int layerIndex, const utils::PointT<int> lower, const bool reverse) {
    // edgeLength 当前普通边长度；reverse=true 需求减 1、totalLength 减长度，否则相反。
    unsigned direction = layerDirections[layerIndex];
    DBU edgeLength = getEdgeLength(direction, lower[direction]);
    if (reverse) {
        commit(layerIndex, lower, -1);
        totalLength -= edgeLength;
    } else {
        commit(layerIndex, lower, 1);
        totalLength += edgeLength;
    }
}

void GridGraph::commitVia(const int layerIndex, const utils::PointT<int> loc, const bool reverse) {
    // 局部量与 getViaCost 相同，但此处真的改 demand。reverse 控制附加需求加/减。
    // 每相邻层调用记一个过孔；跨多层的数量由 commitTree 的逐层循环决定。
    // 与 getViaCost 使用相同的最小面积需求估计，保证评分与资源登记口径对应。
    // (reverse ? -demand : demand) 是三目运算：拆线用负数，布线用正数。
    assert(layerIndex + 1 < nLayers);
    for (int l = layerIndex; l <= layerIndex + 1; l++) {
        unsigned direction = layerDirections[l];
        utils::PointT<int> lowerLoc = loc;
        lowerLoc[direction] -= 1;
        DBU lowerEdgeLength = loc[direction] > 0 ? getEdgeLength(direction, lowerLoc[direction]) : 0;
        DBU higherEdgeLength = loc[direction] < getSize(direction) - 1 ? getEdgeLength(direction, loc[direction]) : 0;
        CapacityT demand = (CapacityT) layerMinLengths[l] / (lowerEdgeLength + higherEdgeLength) * parameters.via_multiplier;
        if (lowerEdgeLength > 0) commit(l, lowerLoc, (reverse ? -demand : demand));
        if (higherEdgeLength > 0) commit(l, loc, (reverse ? -demand : demand));
    }
    if (reverse) totalNumVias -= 1;
    else totalNumVias += 1;
}

// 将树的长边展开到每条网格边；跨多层连接展开成逐层过孔。reverse 用于拆线。
void GridGraph::commitTree(const std::shared_ptr<GRTreeNode>& tree, const bool reverse) {
    // tree 结果树根，node/child 父/子点；direction 同层方向；l/h 走线轴低/高端。
    // 同层展开 [l,h) 各单边；异层用 layerIdx 在低层到高层之间逐个提交相邻层过孔。
    GRTreeNode::preorder(tree, [&](std::shared_ptr<GRTreeNode> node) {
        for (const auto& child : node->children) {
            if (node->layerIdx == child->layerIdx) {
                unsigned direction = layerDirections[node->layerIdx];
                if (direction == MetalLayer::H) {
                    assert(node->y == child->y);
                    int l = min(node->x, child->x), h = max(node->x, child->x);
                    for (int x = l; x < h; x++) {
                        commitWire(node->layerIdx, {x, node->y}, reverse);
                    }
                } else {
                    assert(node->x == child->x);
                    int l = min(node->y, child->y), h = max(node->y, child->y);
                    for (int y = l; y < h; y++) {
                        commitWire(node->layerIdx, {node->x, y}, reverse);
                    }
                }
            } else {
                int maxLayerIndex = max(node->layerIdx, child->layerIdx);
                for (int layerIdx = min(node->layerIdx, child->layerIdx); layerIdx < maxLayerIndex; layerIdx++) {
                    commitVia(layerIdx, {node->x, node->y}, reverse);
                }
            }
        }
    });
}

int GridGraph::checkOverflow(const int layerIndex, const utils::PointT<int> u, const utils::PointT<int> v) const {
    int num = 0;
    unsigned direction = layerDirections[layerIndex];
    if (direction == MetalLayer::H) {
        assert(u.y == v.y);
        int l = min(u.x, v.x), h = max(u.x, v.x);
        for (int x = l; x < h; x++) {
            if (checkOverflow(layerIndex, x, u.y)) num++;
        }
    } else {
        assert(u.x == v.x);
        int l = min(u.y, v.y), h = max(u.y, v.y);
        for (int y = l; y < h; y++) {
            if (checkOverflow(layerIndex, u.x, y)) num++;
        }
    }
    return num;
}

int GridGraph::checkOverflow(const std::shared_ptr<GRTreeNode>& tree) const {
    if (!tree) return 0;
    int num = 0;
    GRTreeNode::preorder(tree, [&](std::shared_ptr<GRTreeNode> node) {
        for (auto& child : node->children) {
            // Only check wires
            if (node->layerIdx == child->layerIdx) {
                num += checkOverflow(node->layerIdx, (utils::PointT<int>)*node, (utils::PointT<int>)*child);
            }
        }
    });
    return num;
}


std::string GridGraph::getPythonString(const std::shared_ptr<GRTreeNode>& routingTree) const {
    vector<std::tuple<utils::PointT<int>, utils::PointT<int>, bool>> edges;
    GRTreeNode::preorder(routingTree, [&] (std::shared_ptr<GRTreeNode> node) {
        for (auto& child : node->children) {
            if (node->layerIdx == child->layerIdx) {
                unsigned direction = getLayerDirection(node->layerIdx);
                int r = (*node)[1 - direction];
                const int l = min((*node)[direction], (*child)[direction]);
                const int h = max((*node)[direction], (*child)[direction]);
                if (l == h) continue;
                utils::PointT<int> lpoint = (direction == MetalLayer::H ? utils::PointT<int>(l, r) : utils::PointT<int>(r, l));
                utils::PointT<int> hpoint = (direction == MetalLayer::H ? utils::PointT<int>(h, r) : utils::PointT<int>(r, h));
                bool congested = false;
                for (int c = l; c < h; c++) {
                    utils::PointT<int> cpoint = (direction == MetalLayer::H ? utils::PointT<int>(c, r) : utils::PointT<int>(r, c));
                    if (checkOverflow(node->layerIdx, cpoint.x, cpoint.y) != congested) {
                        if (lpoint != cpoint) {
                            edges.emplace_back(lpoint, cpoint, congested);
                            lpoint = cpoint;
                        }
                        congested = !congested;
                    } 
                }
                if (lpoint != hpoint) edges.emplace_back(lpoint, hpoint, congested);
            }
        }
    });
    std::stringstream ss;
    ss << "[";
    for (int i = 0; i < edges.size(); i++) {
        auto& edge = edges[i];
        ss << "[" << std::get<0>(edge) << ", " << std::get<1>(edge) << ", " << (std::get<2>(edge) ? 1 : 0) << "]";
        ss << (i < edges.size() - 1 ? ", " : "]");
    }
    return ss.str();
}

void GridGraph::extractBlockageView(GridGraphView<bool>& view) const {
    // view[direction][x][y] 为阻塞标志：初始 true，任一同向真实层容量>=1 则 false。
    // 初始假定两个方向都阻塞；只要该方向有某层容量>=1，就认为有可走资源。
    // 此视图关注固定容量，不依当前 demand 判断是否拥塞。
    view.assign(2, vector<vector<bool>>(xSize, vector<bool>(ySize, true)));
    for (int layerIndex = parameters.min_routing_layer; layerIndex < nLayers; layerIndex++) {
        unsigned direction = getLayerDirection(layerIndex);
        for (int x = 0; x < xSize; x++) {
            for (int y = 0; y < ySize; y++) {
                if (getEdge(layerIndex, x, y).capacity >= 1.0) {
                    view[direction][x][y] = false;
                }
            }
        }
    }
}

// 同方向任一层发生溢出，就将该二维边标记为拥塞，供绕行候选生成使用。
void GridGraph::extractCongestionView(GridGraphView<bool>& view) const {
    // view 同索引为拥塞标志：初始 false，任一同向真实层 resource<0 则 true。
    // 因此 blockage 和 congestion 虽都用 bool，含义不同，不能直接互换。
    view.assign(2, vector<vector<bool>>(xSize, vector<bool>(ySize, false)));
    for (int layerIndex = parameters.min_routing_layer; layerIndex < nLayers; layerIndex++) {
        unsigned direction = getLayerDirection(layerIndex);
        for (int x = 0; x < xSize; x++) {
            for (int y = 0; y < ySize; y++) {
                if (checkOverflow(layerIndex, x, y)) {
                    view[direction][x][y] = true;
                }
            }
        }
    }
}

// 先累加同方向各层的容量与需求，再计算二维边代价，减少迷宫搜索状态数量。
void GridGraph::extractWireCostView(GridGraphView<CostT>& view) const {
    // layerIndices 同方向真实层列表；unitLengthShortCost 它们中最低短路长度系数。
    // capacity/demand 对同向层求和；length 当前边 DBU 长度；edgeIndex 沿方向的低端索引。
    // 例：两层水平金属的容量分别 2/3，需求 1/2，则聚合容量 5、需求 3。
    // 迷宫搜索先按这个合并模型选走廊，随后真实层分配还要查各层单独代价。
    view.assign(2, vector<vector<CostT>>(xSize, vector<CostT>(ySize, std::numeric_limits<CostT>::max())));
    for (unsigned direction = 0; direction < 2; direction++) {
        vector<int> layerIndices;
        CostT unitLengthShortCost = std::numeric_limits<CostT>::max();
        for (int layerIndex = parameters.min_routing_layer; layerIndex < getNumLayers(); layerIndex++) {
            if (getLayerDirection(layerIndex) == direction) {
                layerIndices.emplace_back(layerIndex);
                unitLengthShortCost = min(unitLengthShortCost, getUnitLengthShortCost(layerIndex));
            }
        }
        for (int x = 0; x < xSize; x++) {
            for (int y = 0; y < ySize; y++) {
                int edgeIndex = direction == MetalLayer::H ? x : y;
                if (edgeIndex >= getSize(direction) - 1) continue;
                CapacityT capacity = 0;
                CapacityT demand = 0;
                for (int layerIndex : layerIndices) {
                    const auto& edge = getEdge(layerIndex, x, y);
                    capacity += edge.capacity;
                    demand += edge.demand;
                }
                DBU length = getEdgeLength(direction, edgeIndex);
                view[direction][x][y] = length * (
                    unit_length_wire_cost + unitLengthShortCost * (
                        capacity < 1.0 ? 1.0 : logistic(capacity - demand, parameters.maze_logistic_slope)
                    )
                );
            }
        }
    }
}

// 新树提交后只刷新受其导线/过孔影响的位置，供下一条线网使用最新代价。
void GridGraph::updateWireCostView(GridGraphView<CostT>& view, std::shared_ptr<GRTreeNode> routingTree) const {
    // sameDirectionLayers[H/V] 同向层表，unitLengthShortCost[H/V] 最低短路系数。
    // update 小函数重新计算一个位置聚合分；routingTree 遍历仅定位受新树影响的位置。
    vector<vector<int>> sameDirectionLayers(2);
    vector<CostT> unitLengthShortCost(2, std::numeric_limits<CostT>::max());
    for (int layerIndex = parameters.min_routing_layer; layerIndex < getNumLayers(); layerIndex++) {
        unsigned direction = getLayerDirection(layerIndex);
        sameDirectionLayers[direction].emplace_back(layerIndex);
        unitLengthShortCost[direction] = min(unitLengthShortCost[direction], getUnitLengthShortCost(layerIndex));
    }
    auto update = [&] (unsigned direction, int x, int y) {
        int edgeIndex = direction == MetalLayer::H ? x : y;
        if (edgeIndex >= getSize(direction) - 1) return;
        CapacityT capacity = 0;
        CapacityT demand = 0;
        for (int layerIndex : sameDirectionLayers[direction]) {
            if (getLayerDirection(layerIndex) != direction) continue;
            const auto& edge = getEdge(layerIndex, x, y);
            capacity += edge.capacity;
            demand += edge.demand;
        }
        DBU length = getEdgeLength(direction, edgeIndex);
        view[direction][x][y] = length * (
            unit_length_wire_cost + unitLengthShortCost[direction] * (
                capacity < 1.0 ? 1.0 : logistic(capacity - demand, parameters.maze_logistic_slope)
            )
        );
    };
    GRTreeNode::preorder(routingTree, [&] (std::shared_ptr<GRTreeNode> node) {
        for (const auto& child : node->children) {
            if (node->layerIdx == child->layerIdx) {
                unsigned direction = getLayerDirection(node->layerIdx);
                if (direction == MetalLayer::H) {
                    assert(node->y == child->y);
                    int l = min(node->x, child->x), h = max(node->x, child->x);
                    for (int x = l; x < h; x++) {
                        update(direction, x, node->y);
                    }
                } else {
                    assert(node->x == child->x);
                    int l = min(node->y, child->y), h = max(node->y, child->y);
                    for (int y = l; y < h; y++) {
                        update(direction, node->x, y);
                    }
                }
            } else {
                int maxLayerIndex = max(node->layerIdx, child->layerIdx);
                for (int layerIdx = min(node->layerIdx, child->layerIdx); layerIdx <= maxLayerIndex; layerIdx++) {
                    unsigned direction = getLayerDirection(layerIdx);
                    update(direction, node->x, node->y);
                    if ((*node)[direction] > 0) update(direction, node->x - 1 + direction, node->y - direction);
                }
            }
        }
    });
}

// 输出各层 capacity-demand，负数表示需求超过容量，与 visualize_heatmap.py 的输入一致。
void GridGraph::write(const std::string heatmap_file) const {
    log() << "writing heatmap to file..." << std::endl;
    std::stringstream ss;
    
    ss << nLayers << " " << xSize << " " << ySize << " " << std::endl;
    for (int layerIndex = 0; layerIndex < nLayers; layerIndex++) {
        ss << layerNames[layerIndex] << std::endl;
        for (int y = 0; y < ySize; y++) {
            for (int x = 0; x < xSize; x++) {
                ss << (graphEdges[layerIndex][x][y].capacity - graphEdges[layerIndex][x][y].demand)
                     << (x == xSize - 1 ? "" : " ");
            }
            ss << std::endl;
        }
    }
    std::ofstream fout(heatmap_file);
    fout << ss.str();
    fout.close();
}
