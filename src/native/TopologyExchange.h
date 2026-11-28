#pragma once
#include "IncrementalRoute.h"
#include "TopologyRefinement.h"

namespace native3d {
struct CoordinatedStage {
    size_t attempted=0,completed=0,accepted=0,timedOut=0,candidates=0;
    Json statistics;
};
CoordinatedStage runTopologyExchange(IncumbentSolution&,const std::vector<std::pair<double,int>>&,
    const std::vector<std::vector<int>>&,const Deadline&,double netSeconds,int stride,const std::string& outputDirectory,
    std::map<std::string,TopologyStats>&);
}
