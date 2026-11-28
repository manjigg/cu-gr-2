#pragma once
#include "PatternSearch.h"

namespace native3d {
struct ConflictGroups {
    std::vector<std::vector<int>> groups;
    Occupancy prices;
    size_t contested=0;
};
ConflictGroups buildConflictGroups(const RoutingGraph&,const std::vector<std::vector<PreparedRoute>>&,
                                   size_t maxGroup,double step);

struct LocalResult {
    std::vector<size_t> selection;
    bool complete=true;
    size_t visited=0;
    size_t combinations=0,pruned=0;
    size_t bestExcess=0;
    long double bestCost=0;
};
// 分量内部尚未提交，分量外部资源仍在原图中占用。返回时资源账本恢复为入口状态。
LocalResult selectLocal(RoutingGraph&,const std::vector<int>&,const std::vector<std::vector<PreparedRoute>>&,
                        const Deadline&,bool protectHbt=false,size_t hbtRoom=std::numeric_limits<size_t>::max());
// 默认零号为当前已提交解。穷举模式供独立有限候选验证。
LocalResult selectGroup(RoutingGraph&,const std::vector<int>&,const std::vector<std::vector<PreparedRoute>>&,
                        const Deadline&,size_t hbtOutside,size_t hbtBudget,bool exhaustive=false);
}
