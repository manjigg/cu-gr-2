// 文件功能：实现 H3D 第 III-B 节的 die 分配，最小化给定平面树上的拓扑 HBT 数量。
// 此步骤不分配实际 HBT 站点，也不选择 die 内的金属层。
#pragma once

#include "H3DTree.h"

namespace h3d {

struct DieSpan {
    Die low;
    Die high;
};

struct DieAssignmentResult {
    int root = -1;
    int hbtCount = 0;
    std::vector<Die> edgeDies;    // 与 H3DTree::getEdges() 的编号一一对应。
    std::vector<DieSpan> nodeDieSpans;
    std::vector<int> hbtNodes;    // 两个 die 都需连接的节点，每节点仅计一个 HBT。
};

class DieAssignment {
public:
    // 对固定拓扑求精确最少 HBT 数量；正向回溯保留边的 die 与 HBT 插入节点。
    // 默认根与同分选择均为确定性规则，方便重现及后续算法比较。
    // tiePreference 只决定等价最小值的 die 偏好，不改变公式 (3)(4) 的成本。
    DieAssignmentResult run(const H3DTree& tree, int root = -1, Die tiePreference = Die::Bottom,
                            std::uint32_t tieSeed = 0) const;
};

} // namespace h3d
