// 文件功能：定义稀疏采样网格、方向双平面图及迷宫布线搜索状态。
// 搜索输出二维 Steiner 拓扑，由 PatternRoute 再做真实金属层分配。
//
// 【白话职责】规则路径还是挤时，把候选道路组织成图，从已有连通区域寻找低价新路。
// SparseGrid 是“隔几格取道路、从哪里开始取”的采样参数。
// SparseGraph 是“采样后哪些地点可以相连、每次走动多少钱”的图。
// Solution 是搜索中的一张小纸条：累计分数、当前地点、上一张纸条。
// MazeRoute 保存这些纸条，接通全部伪引脚后往回追，恢复二维连通树。
// 图里的两平面只表示方向，不是芯片只有两层金属，也没有在这里确定最终层。
#pragma once
#include "global.h"
#include "GridGraph.h"
#include "GRNet.h"
#include "PatternRoute.h"

// interval 控制采样间隔；offset 在不同线网间轮换，改变可选的稀疏网格位置。
struct SparseGrid {
    // interval.x/y 原格子采样间隔，offset.x/y 此轮起点偏移；均为网格索引尺度。
    utils::PointT<int> interval;
    utils::PointT<int> offset;
    SparseGrid(int xInterval, int yInterval, int xOffset, int yOffset):
        interval(xInterval, yInterval), offset(xOffset, yOffset) {}
    void step() {
        // % 是取余。例如 interval=10，偏移从 9 加到 10 后取余回到 0。
        // 因此不同线网可以轮换 0..9 的道路采样位置。
        offset.x = (offset.x + 1) % interval.x;
        offset.y = (offset.y + 1) % interval.y;
    }
    void reset(int xInterval, int yInterval) {
        // xInterval/yInterval 新间隔；reset 仍调用 step 推进偏移，不是归零偏移。
        interval.x = xInterval;
        interval.y = yInterval;
        step();
    }
};

// 两个平面分别代表 H/V 方向；每个顶点最多有前进、后退、切换方向三条边。
class SparseGraph {
public:
    SparseGraph(GRNet& _net, const GridGraph& graph3d, const Parameters& param):
        net(_net), gridGraph(graph3d), parameters(param) {}
    void init(GridGraphView<CostT>& wireCostView, SparseGrid& grid);
    int getNumVertices() const { return vertices.size(); }
    int getNumPseudoPins() const { return pseudoPins.size(); }
    std::pair<utils::PointT<int>, utils::IntervalT<int>> getPseudoPin(int pinIndex) const { return pseudoPins[pinIndex]; }
    int getPinVertex(const int pinIndex) const { return pinVertex[pinIndex]; }
    int getVertexPin(const int vertex) const {
        auto it = vertexPin.find(vertex);
        return it == vertexPin.end() ? -1 : it->second;
    }
    int getNextVertex(const int vertex, const int edgeIndex) const { return edges[vertex][edgeIndex]; }
    CostT getEdgeCost(const int vertex, const int edgeIndex) const { return costs[vertex][edgeIndex]; }
    GRPoint getPoint(const int vertex) const { return vertices[vertex]; }
    
private:
    // parameters 算法设置，gridGraph 共享三维账本，net 本次要改善的线网。
    const Parameters& parameters;
    const GridGraph& gridGraph;   
    GRNet& net;
    
    // pseudoPins[pin]：(二维格子位置,必连真实层区间)，同二维位置的引脚合并为一项。
    vector<std::pair<utils::PointT<int>, utils::IntervalT<int>>> pseudoPins;
    
    // xs[xi]/ys[yi]：采样下标 xi/yi 对应的原图格子坐标，数值不必等于下标。
    vector<int> xs;
    vector<int> ys;
    
    // vertices[v] 搜索顶点，GRPoint.layerIdx 借来表示 H/V，x/y 为原格子位置。
    vector<GRPoint> vertices;
    vector<std::array<int, 3>> edges;
    // array<int,3> 是固定三个槽：0/1 为本方向前后邻居，2 为切换方向的邻居。
    // 邻居为 -1 时表示该槽没有路；costs 中相同槽存相应走动代价。
    vector<std::array<CostT, 3>> costs;
    // robin_hood::unordered_map<int, vector<int>> vertexPins;
    // vector<vector<int>> pinVertices;
    // vertexPin：“顶点编号->伪引脚编号”字典，非引脚顶点查询为 -1。
    robin_hood::unordered_map<int, int> vertexPin;
    // pinVertex[pin]：“伪引脚编号->搜索顶点编号”，与前项查询方向相反。
    vector<int> pinVertex;
    
    inline int getVertexIndex(int direction, int xi, int yi) const {
        // 将方向、采样 x 下标、采样 y 下标编码成连续顶点编号。
        // xi/yi 是 xs/ys 中的位置，不一定等于原图的 x/y 格子编号。
        return direction * xs.size() * ys.size() + yi * xs.size() + xi;
    }
    
};

struct Solution {
    // prev 是前一个搜索状态的地址，像面包屑，找到目标后靠它回溯整条路。
    // cost：自当前连通树零代价源扩展的累计分，不是最终整网三维成本。
    CostT cost;
    // vertex：搜索顶点编号，用 graph.getPoint 取出实际位置/方向。
    int vertex;
    std::shared_ptr<Solution> prev;
    // c/v/p 分别为传入 cost/vertex/prev，下划线或缩写只是参数与成员的区分方式。
    Solution(CostT c, int v, const std::shared_ptr<Solution> &p): cost(c), vertex(v), prev(p) {}
};

class MazeRoute {
public:
    MazeRoute(GRNet& _net, const GridGraph& graph3d, const Parameters& param):
        net(_net), gridGraph(graph3d), parameters(param), graph(_net, graph3d, param) {}
    
    void run();
    void constructSparsifiedGraph(GridGraphView<CostT>& wireCostView, SparseGrid& grid) {
        graph.init(wireCostView, grid);
    }
    std::shared_ptr<SteinerTreeNode> getSteinerTree() const;
    
private: 
    const Parameters& parameters;
    const GridGraph& gridGraph;   
    GRNet& net;
    // graph 本线网临时稀疏方向图，区别于共享三维 gridGraph。
    SparseGraph graph;
    
    // solutions[i] 第 i 次接入新伪引脚的路径终点状态，沿 prev 链恢复路径。
    vector<std::shared_ptr<Solution>> solutions;
};
