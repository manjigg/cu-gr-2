#pragma once
#include "HbtLocationOptimizer.h"

namespace h3d {

struct DieComponent {
    Die die;
    std::vector<int> nodes;
    std::vector<int> edges;
    std::vector<int> hbtSites;
};

struct SubnetPartition {
    int root = -1;
    std::vector<int> reverseDfsOrder;
    std::vector<DieComponent> components;
};

// III-D：逆 DFS 中连接同 die 的树边，在 HBT 的上下 die 副本之间切开。
// 只把同网同 HBT 站点作为同一个终端；不同拓扑分量的普通同坐标点不自动相连。
SubnetPartition partitionAtHbts(const AssignedTopology& topology, const LocationResult& locations);

} // namespace h3d
