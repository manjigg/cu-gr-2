#pragma once
#include "IncrementalRoute.h"

namespace native3d {
Json repairFailedNets(IncumbentSolution&,const std::vector<std::pair<double,int>>& priority,
                      const Deadline&,double netSeconds,int stride);
}
