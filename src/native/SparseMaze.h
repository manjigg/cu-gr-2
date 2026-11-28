#pragma once
#include "PatternSearch.h"

namespace native3d {
// 逐终端连接生成候选拓扑。金属层、Die 与 HBT 站点在回溯中保留。
struct MazeEffort {
    uint64_t expanded=0;
    bool refineTimeout=false,recordBlockers=false;
    std::map<Resource,unsigned> blocked;
    void observe(const std::vector<Resource>& resources);
};
class SparseMaze {
public:
    SparseMaze(const RoutingGraph& graph,int netId,const Deadline& deadline,const ResourceReadView* snapshot=nullptr,MazeEffort* counters=nullptr):
        graph(graph),net(netId),time(deadline),view(snapshot),effort(counters) {}
    Routes candidates(int stride,bool allowDieDetour) const;
    Routes seed(int stride,bool allowDieDetour) const;
private:
    const RoutingGraph& graph;
    int net;
    const Deadline& time;
    const ResourceReadView* view;
    MazeEffort* effort;
    bool grow(Route& tree,int stride,bool allowDieDetour,bool bounded) const;
};
}
