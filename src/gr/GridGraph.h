// 文件功能：定义三维布线资源网格 GridGraph、容量/需求边 GraphEdge 和二维方向视图。
// GridGraph 管理几何映射、线/过孔代价及需求提交；GridGraphView 按 [方向][x][y] 索引。
//
// 【白话职责】GridGraph 是所有路由器共同查询的“道路地图+占用账本”。
// 芯片划成许多格子（GCell），同一层相邻格子之间的边代表可通过的导线资源。
// GraphEdge.capacity 是道路大约能容纳多少条线，demand 是当前方案用了多少。
// 例：容量 3，登记两条普通导线后需求 2，余额 1；再登记两条后余额 -1。
// 图的数组按 [真实金属层][x][y] 保存边；每层只沿自己的首选方向走。
// 水平层的 (x,y) 保存向 (x+1,y) 的边；垂直层保存向 (x,y+1) 的边。
// 另一种 GridGraphView 把所有层压成 H/V 两个方向，用于快速找拥塞/搜索。
// 图不会主动布线；PatternRoute/MazeRoute 查询它，总调度器更新它。
#pragma once
#include "global.h"
#include "obj/Design.h"
#include "GRTree.h"

using CapacityT = double;
class GRNet;
template <typename Type> class GridGraphView;

// capacity 是扣除障碍后的可用轨道数（可能为小数），demand 是已提交的布线需求。
struct GraphEdge {
    // capacity：固定障碍扣减后的估计可容纳条数，可为小数，默认 0。
    CapacityT capacity;
    // demand：已提交普通线与过孔附加线的需求；普通导线经过一条边增加 1。
    CapacityT demand;
    GraphEdge(): capacity(0), demand(0) {}
    CapacityT getResource() const { return capacity - demand; }
    // const 说明这是只读查询，不会因计算余额改变 capacity 或 demand。
};

class GridGraph {
public:
    GridGraph(const Design& design, const Parameters& params);
    inline DBU getLibDBU() const { return libDBU; }
    inline DBU getM2Pitch() const { return m2_pitch; }
    inline unsigned getNumLayers() const { return nLayers; }
    inline unsigned getSize(unsigned dimension) const { return (dimension ? ySize : xSize); }
    inline std::string getLayerName(int layerIndex) const { return layerNames[layerIndex]; }
    inline unsigned getLayerDirection(int layerIndex) const { return layerDirections[layerIndex]; }
    
    inline uint64_t hashCell(const GRPoint& point) const {
        // 三维地址压成一维编号：先跨层，再跨 x，再加 y。
        // 在有效网格范围内，不同地址得到不同编号，适合作字典/集合的键。
        return ((uint64_t)point.layerIdx * xSize + point.x) * ySize + point.y;
    };
    inline uint64_t hashCell(const int x, const int y) const { return (uint64_t)x * ySize + y; }
    inline DBU getGridline(const unsigned dimension, const int index) const { return gridlines[dimension][index]; }
    utils::BoxT<DBU> getCellBox(utils::PointT<int> point) const;
    utils::BoxT<int> rangeSearchCells(const utils::BoxT<DBU>& box) const;
    // 此处返回边的副本供查看，不是可修改引用；改需求应通过 commit* 接口。
    inline GraphEdge getEdge(const int layerIndex, const int x, const int y) const {return graphEdges[layerIndex][x][y]; }
    
    // Costs
    DBU getEdgeLength(unsigned direction, unsigned edgeIndex) const;
    CostT getWireCost(const int layerIndex, const utils::PointT<int> u, const utils::PointT<int> v) const;
    CostT getViaCost(const int layerIndex, const utils::PointT<int> loc) const;
    inline CostT getUnitViaCost() const { return unit_via_cost; }
    
    // Misc
    void selectAccessPoints(GRNet& net, robin_hood::unordered_map<uint64_t, std::pair<utils::PointT<int>, utils::IntervalT<int>>>& selectedAccessPoints) const;
    
    // Methods for updating demands 
    void commitTree(const std::shared_ptr<GRTreeNode>& tree, const bool reverse = false);
    
    // Checks
    inline bool checkOverflow(const int layerIndex, const int x, const int y) const { return getEdge(layerIndex, x, y).getResource() < 0.0; }
    int checkOverflow(const int layerIndex, const utils::PointT<int> u, const utils::PointT<int> v) const; // Check wire overflow
    int checkOverflow(const std::shared_ptr<GRTreeNode>& tree) const; // Check routing tree overflow (Only wires are checked)
    std::string getPythonString(const std::shared_ptr<GRTreeNode>& routingTree) const;
    
    // 2D maps
    void extractBlockageView(GridGraphView<bool>& view) const;
    void extractCongestionView(GridGraphView<bool>& view) const; // 2D overflow look-up table
    void extractWireCostView(GridGraphView<CostT>& view) const;
    void updateWireCostView(GridGraphView<CostT>& view, std::shared_ptr<GRTreeNode> routingTree) const;
    
    // For visualization
    void write(const std::string heatmap_file="heatmap.txt") const;
    
private:
    // libDBU 每微米 DBU 换算，m2_pitch 层索引 1 的轨距，parameters 只读设置。
    const DBU libDBU;
    DBU m2_pitch;
    const Parameters& parameters;
    
    // nLayers 真实布线层数量，xSize/ySize 两轴格子数，都不是物理长度。
    unsigned nLayers;
    unsigned xSize;
    unsigned ySize;
    // gridlines[dimension][i]：轴 dimension 的第 i 条 DBU 边界，0=x、1=y。
    vector<vector<DBU>> gridlines;
    // gridCenters[dimension][i]：该轴第 i 格中心 DBU 坐标，数量比边界少一。
    vector<vector<DBU>> gridCenters;
    // layerNames[layer] 层名，layerDirections[layer] H/V，layerMinLengths[layer] 附加线长。
    vector<std::string> layerNames;
    vector<unsigned> layerDirections;
    vector<DBU> layerMinLengths;
    
    // Unit costs
    // 以下分别为每 DBU 普通线长分、每过孔基础分、每层每 DBU 短路面积系数。
    CostT unit_length_wire_cost;
    CostT unit_via_cost;
    vector<CostT> unit_length_short_costs;
    
    // totalLength 已登记普通线 DBU 长度；totalNumVias 已登记相邻层过孔数。
    // 提交加、撤回减；最终路由统计还会另遍历结果树。
    DBU totalLength = 0;
    int totalNumVias = 0;
    // 三层 vector 并非三个图，而是层/x/y 三个索引维度。
    // graphEdges[l][x][y] 取出一个 GraphEdge，里面才有 capacity 与 demand 两个数。
    vector<vector<vector<GraphEdge>>> graphEdges;
    // gridEdges[l][x][y] stores the edge {(l, x, y), (l, x+1, y)} or {(l, x, y), (l, x, y+1)}
    // depending on the routing direction of the layer
    
    utils::IntervalT<int> rangeSearchGridlines(const unsigned dimension, const utils::IntervalT<DBU>& locInterval) const;
    // Find the gridlines within [locInterval.low, locInterval.high]
    utils::IntervalT<int> rangeSearchRows(const unsigned dimension, const utils::IntervalT<DBU>& locInterval) const;
    // Find the rows/columns overlapping with [locInterval.low, locInterval.high]
    
    // Utility functions for cost calculation
    inline CostT getUnitLengthWireCost() const { return unit_length_wire_cost; }
    // CostT getUnitViaCost() const { return unit_via_cost; }
    inline CostT getUnitLengthShortCost(const int layerIndex) const { return unit_length_short_costs[layerIndex]; }
    
    inline double logistic(const CapacityT& input, const double slope) const;
    CostT getWireCost(const int layerIndex, const utils::PointT<int> lower, const CapacityT demand = 1.0) const;
    
    // Methods for updating demands 
    void commit(const int layerIndex, const utils::PointT<int> lower, const CapacityT demand);
    void commitWire(const int layerIndex, const utils::PointT<int> lower, const bool reverse = false);
    void commitVia(const int layerIndex, const utils::PointT<int> loc, const bool reverse = false);
};

// 仅保留 H/V 两个方向，供绕行检查和迷宫搜索使用，不等同于两个实际金属层。
template <typename Type>
class GridGraphView: public vector<vector<vector<Type>>> {
    // template 的 Type 是“格子里存什么”：bool 表示是/否，CostT 表示代价小数。
    // 继承 vector 后可以像普通三维列表一样用 [] 下标访问。
public:
    bool check(const utils::PointT<int>& u, const utils::PointT<int>& v) const {
        // 只接受直线两端。若同 y 就扫描水平边，否则扫描垂直边。
        // x<h/y<h 是边数：从格子 2 到格子 5 经过索引 2/3/4 的三条边。
        // (*this) 是当前视图对象；this 是指针，* 是取出指针所指对象。
        assert(u.x == v.x || u.y == v.y);
        if (u.y == v.y) {
            int l = min(u.x, v.x), h = max(u.x, v.x);
            for (int x = l; x < h; x++) {
                if ((*this)[MetalLayer::H][x][u.y]) return true;
            }
        } else {
            int l = min(u.y, v.y), h = max(u.y, v.y);
            for (int y = l; y < h; y++) {
                if ((*this)[MetalLayer::V][u.x][y]) return true;
            }
        }
        return false;
    }
    
    Type sum(const utils::PointT<int>& u, const utils::PointT<int>& v) const {
        // u/v 两端格子位置；res 累计分；l/h 沿走线轴的小/大下标。
        // x 或 y 是当前边的低坐标端，另一个轴保持不变；终端 h 没有多余边。
        // 与 check 的扫描路线相同，但不是发现真值就返回，而是逐边累加数值。
        // 例如三条边代价 2/5/3，这段直线的总分就是 10。
        assert(u.x == v.x || u.y == v.y);
        Type res = 0;
        if (u.y == v.y) {
            int l = min(u.x, v.x), h = max(u.x, v.x);
            for (int x = l; x < h; x++) {
                res += (*this)[MetalLayer::H][x][u.y];
            }
        } else {
            int l = min(u.y, v.y), h = max(u.y, v.y);
            for (int y = l; y < h; y++) {
                res += (*this)[MetalLayer::V][u.x][y];
            }
        }
        return res;
    }
};
