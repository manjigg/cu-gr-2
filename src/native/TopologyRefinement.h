#pragma once
#include "PatternSearch.h"

namespace native3d {
struct TopologyStats {
    size_t attempted=0,generated=0,feasible=0,accepted=0,capacity=0,connectivity=0,timedOut=0,budget=0,cost=0;
    double seconds=0,deltaWire=0,deltaVia=0,deltaHbt=0,deltaCost=0;
    size_t staticLegal=0,submittable=0,retained=0,jointAccepted=0;
    Json report() const;
};
struct TopologyCandidate {std::string operation;Route route;};
std::vector<TopologyCandidate> refineTopology(const RoutingGraph&,int net,const Route& incumbent,
    const Deadline&,std::map<std::string,TopologyStats>&,const ResourceReadView* view=nullptr,
    const std::function<void(const Edges&)>& observe={});
}
