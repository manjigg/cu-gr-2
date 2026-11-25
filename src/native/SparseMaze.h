#pragma once
#include "PatternSearch.h"

namespace native3d {
// 逐终端连接生成候选拓扑。金属层、Die 与 HBT 站点在回溯中保留。
class SparseMaze {
public:
    SparseMaze(const RoutingGraph& graph,int netId,const Deadline& deadline):
        graph(graph),net(netId),time(deadline) {}
    Routes candidates(int stride,bool allowDieDetour) const;
    Routes seed(int stride,bool allowDieDetour) const;
private:
    const RoutingGraph& graph;
    int net;
    const Deadline& time;
    bool grow(Route& tree,int stride,bool allowDieDetour,bool bounded) const;
};
}
