#pragma once
#include "RoutingGraph.h"
namespace native3d {
Json writeSolution(const RoutingGraph&,const std::vector<Route>&,const std::string& output);
}
