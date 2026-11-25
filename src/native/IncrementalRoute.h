#pragma once
#include "PatternSearch.h"

namespace native3d {
struct IncrementalResult {
    Routes routes;
    std::vector<int> unrouted;
};
IncrementalResult routeIncrementally(RoutingGraph&,const std::vector<int>& stages,Json& report);
}
