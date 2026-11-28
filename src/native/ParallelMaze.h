#pragma once
#include "IncrementalRoute.h"

namespace native3d {
// 阶段结束后返回真实统计。priority 的顺序也是确定性提交的顺序。
Json routeMazeBatches(IncumbentSolution&,const std::vector<std::pair<double,int>>& priority,
                      const Deadline& stage,int stride,double netSeconds,unsigned workers);
}
