#pragma once
#include "PatternSearch.h"
namespace native3d {
// 为精确候选选择器提供可行上界。多 HBT 由跨 Die 的生成树分支自动产生。
Routes initialCandidates(const RoutingGraph&,int net,const Deadline&);
}
