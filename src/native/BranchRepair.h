#pragma once
#include "PatternSearch.h"
namespace native3d {
// 从现有局部模式树保留可行森林，只对割断的分支做单位资源图搜索，返回完整的局部分量。
Routes repairLocalBranches(const RoutingGraph&,int net,const Route& candidate,
                          const std::set<GRPoint>& required,const Deadline&);
}
