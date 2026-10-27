// 文件功能：保存跨 die 平面拓扑、固定引脚的 die 身份，并提供可重新选根的树遍历。
// position 使用物理 DBU；Die 与单 die 路由器的金属层 layerIdx 是不同的索引域。
#pragma once

#include <vector>
#include <random>

#include "rsyn/util/dbu.h"
#include "utils/geo.h"

namespace h3d {

enum class Die { Bottom = 0, Top = 1 };

struct PinReference {
    Die die;
    int instanceIndex;
    int pinIndex; // 与 Design 中一致：-1 表示外部端口。
};

struct H3DTreeNode {
    utils::PointT<DBU> position;
    // 同一平面位置可包含来自两个 die 的多个固定引脚，不能因坐标重合丢掉 die 信息。
    std::vector<PinReference> pins;
};

struct H3DTreeEdge {
    int u;
    int v;
};

struct RootedTree {
    int root = -1;
    std::vector<int> parents;     // 根的 parent 为 -1。
    std::vector<int> parentEdges; // 原始无向边编号，根为 -1。
    std::vector<std::vector<int>> children;
    std::vector<int> preorder;   // 正序回溯，逆序计算子树 DP。
};

class H3DTree {
public:
    int addNode(utils::PointT<DBU> position, std::vector<PinReference> pins = {});
    int addEdge(int u, int v);

    const std::vector<H3DTreeNode>& getNodes() const { return nodes; }
    const std::vector<H3DTreeEdge>& getEdges() const { return edges; }

    // 检查连通性及无环性；root=-1 时确定性地选择首个度为 1 的节点。
    // 空树返回空遍历，非法节点/根编号或非树输入抛出异常。
    RootedTree rootAt(int root = -1) const;

private:
    std::vector<H3DTreeNode> nodes;
    std::vector<H3DTreeEdge> edges;
};

// III-B：从度为 1 的节点均匀选根；单点退化树以唯一节点为根。
int chooseLeafRoot(const H3DTree& tree, std::mt19937& random);

} // namespace h3d
