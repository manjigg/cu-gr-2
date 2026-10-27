// 文件功能：声明 Steiner 拓扑节点、候选路径 DAG 节点和基于 DAG 动态规划的模式布线器。
// 流程：选择接入点 -> FLUTE 拓扑 -> 直线/L形候选 -> 可选绕行 -> 层代价优化 -> 三维树。
//
// 【白话职责】同样连接两个地点，可能先横后竖，也可能先竖后横，还可绕开拥挤区域。
// 本模块把这些选择先列出来，再连同金属层一起评分，选总分最低的组合。
// SteinerTreeNode 只记录“哪些点接哪些点”；PatternRoutingNode 记录“各连接有哪些走法”；
// 最后生成 GRTreeNode，才得到“选哪种走法、在哪层、哪里放过孔”的结果。
// DAG 是有向无环图：沿候选往孩子走不会绕回原点，但多个候选可以共享后面子图。
// 动态规划就是“先算小问题并记住答案，再组合大问题”，避免共享子图重复求解。
// 初学者先看 public 的流程方法，再看 fields，最后深入 .cpp 的两个评分/回溯函数。
#pragma once
#include "global.h"
#include "GRNet.h"
#include "flute.h"

// FLUTE 用 C 写，调用方用 C++。extern "C" 告诉链接器按 C 的规则查这些函数名。
extern "C" {
void readLUT();
Tree flute(int d, DTYPE x[], DTYPE y[], int acc);
}

// 二维树仅决定连接拓扑；fixedLayers 记录该位置必须连通的引脚层范围。
class SteinerTreeNode: public utils::PointT<int> {
public:
    vector<std::shared_ptr<SteinerTreeNode>> children;
    // children 是子节点指针列表；不是把每棵子树全部复制在这个节点里。
    // fixedLayers 为 [low,high]，表示此处需要连通的最低/最高层。
    utils::IntervalT<int> fixedLayers;
    
    SteinerTreeNode(utils::PointT<int> point): utils::PointT<int>(point) {}
    SteinerTreeNode(utils::PointT<int> point, utils::IntervalT<int> _fixedLayers): 
        utils::PointT<int>(point), fixedLayers(_fixedLayers) {}
        
    static void preorder(std::shared_ptr<SteinerTreeNode> node, std::function<void(std::shared_ptr<SteinerTreeNode>)> visit);
    static std::string getPythonString(std::shared_ptr<SteinerTreeNode> node);
};

// children 表达原树分支，paths 表达每个分支的多个实现；共享节点形成 DAG。
// costs[layer] 缓存该层接入的最优子图代价，bestPaths 保存重建路线需要的选择。
class PatternRoutingNode: public utils::PointT<int> {
public:
    // index：本次 DAG 唯一节点编号，供 visited/scaffoldNodes 按下标定位。
    const int index;
    // int x
    // int y
    vector<std::shared_ptr<PatternRoutingNode>> children;
    vector<vector<std::shared_ptr<PatternRoutingNode>>> paths;
    // paths[0] 是通向第 0 个孩子的候选列表；paths[0][1] 是该分支的第 1 种走法。
    // 候选值是“下一跳”节点，继续沿它的 paths 走，才会抵达目标孩子。
    // childIndex -> pathIndex -> path
    utils::IntervalT<int> fixedLayers; 
    // layers that must be visited in order to connect all the pins 
    // costs[layer]：从该层接入时当前子图的最小分数，空列表意味着尚未求解。
    vector<CostT> costs; // layerIndex -> cost
    // bestPaths[layer][child]：(候选下一跳编号,孩子接入层)，用于回溯，不是空间坐标。
    vector<vector<std::pair<int, int>>> bestPaths;
    // best path for each child; layerIndex -> childIndex -> (pathIndex, layerIndex)
    bool optional;
    // optional=true 通常是某个 L 形候选的拐点，候选不用它时它就不会进入结果树。
    
    PatternRoutingNode(utils::PointT<int> point, int _index, bool _optional = false): 
        utils::PointT<int>(point), index(_index), optional(_optional) {}
    PatternRoutingNode(utils::PointT<int> point, utils::IntervalT<int> _fixedLayers, int _index = 0): 
        utils::PointT<int>(point), fixedLayers(_fixedLayers), index(_index), optional(false) {}
    static std::string getPythonString(std::shared_ptr<PatternRoutingNode> routingDag);
};

class PatternRoute {
public:
    static void readFluteLUT() {
        static const bool loaded = [] { readLUT(); return true; }();
        (void)loaded;
    }
    
    PatternRoute(GRNet& _net, const GridGraph& graph, const Parameters& param):
        // net 是可修改引用，因为最终要把结果树写回；graph/param 是只读借用。
        net(_net), gridGraph(graph), parameters(param), numDagNodes(0) {}
    void constructSteinerTree();
    void constructRoutingDAG();
    void constructDetours(GridGraphView<bool>& congestionView);
    void run();
    void setSteinerTree(std::shared_ptr<SteinerTreeNode> tree) { steinerTree = tree; }
    // 阶段 3 可直接把 MazeRoute 算出的拓扑交进来，省掉重新用 FLUTE 构造拓扑。
    
private:
    // parameters 设置，gridGraph 只读账本，net 当前可写回结果的信号网。
    const Parameters& parameters;
    const GridGraph& gridGraph;
    GRNet& net;
    // numDagNodes 已分配候选节点数，其后置 ++ 为新节点提供唯一编号。
    int numDagNodes;
    // steinerTree 是二维拓扑根，routingDag 是展开后的候选图根，不是三维结果树。
    std::shared_ptr<SteinerTreeNode> steinerTree;
    std::shared_ptr<PatternRoutingNode> routingDag;
    
    // start/end 两端；childIndex=-1 新增分支槽，否则为已有分支增加替代候选。
    void constructPaths(std::shared_ptr<PatternRoutingNode>& start, std::shared_ptr<PatternRoutingNode>& end, int childIndex = -1);
    void calculateRoutingCosts(std::shared_ptr<PatternRoutingNode>& node);
    // node 当前回溯节点；parentLayerIndex=-1 根调用，否则为父连接选择的真实层。
    std::shared_ptr<GRTreeNode> getRoutingTree(std::shared_ptr<PatternRoutingNode>& node, int parentLayerIndex = -1);
};
