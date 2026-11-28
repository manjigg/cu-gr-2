#include "native/RoutingGraph.h"
#include <cmath>

using namespace native3d;
static void require(bool ok,const char* message) {
    if(!ok) throw std::logic_error(message);
}
int main(int argc,char** argv) {
    Parameters parameters(argc,argv);
    MultiDieDesign model(parameters,argv[1]);
    model.config["enable_hbt_footprint"]=true;
    auto& table=model.config["bonding"]["transition_footprints"];
    table["source"]="synthetic test in track and cut units";
    Json upper={{"die",0},{"offset",{0,0}},{"layer_offset",0},{"kind","WIRE"},{"demand",0.75}};
    Json lower={{"die",1},{"offset",{0,0}},{"layer_offset",0},{"kind","VIA"},{"demand",1.5}};
    table["rules"].push_back({{"layers",{"metal2","metal2"}},{"reservations",{upper,lower}}});
    table["rules"].push_back({{"layers",{"metal3","metal3"}},{"reservations",Json::array()}});
    RoutingGraph graph(model);
    const auto h=graph.sites.at(0).transitions.front(),other=graph.sites.at(1).transitions.front();
    auto wireEnd=h.a;
    ++wireEnd[model.dies[0]->grid.getLayerDirection(h.a.layerIdx)];
    auto viaEnd=h.b;++viaEnd.layerIdx;
    const Edge wire(h.a,wireEnd,GREdgeType::WIRE),via(h.b,viaEnd,GREdgeType::VIA);
    const auto w=graph.resourceId(wire),v=graph.resourceId(via);
    const Demand reference{{graph.hbtResource(0),1.},{w,0.75},{v,1.5}};
    require(graph.resources({h})==reference,"physical landing resource mismatch");
    const auto h3=graph.sites.at(0).transitions.at(1);
    require(graph.resources({h3})==Demand{{graph.hbtResource(0),1.}},"transition independence");
    graph.commit(0,{h.a,{h}});
    const auto self=graph.readView(0);
    require(graph.available(0,{h},&self),"self release");
    require(!graph.available(1,{other}) && !std::isfinite(graph.stepCost(1,other)),"metal reservation competition");
    require(!graph.available(1,{via}),"landing consumes actual via capacity");
    require(graph.resources({h,wire}).at(w)==1.75,"additional wire versus routed wire");
    require(graph.available(1,{graph.sites.at(1).transitions.at(1)}),"alternative legal layer pair");
    Edges edges;Demand d;std::vector<int> sites;
    require(graph.extendPath(0,{h},edges,d,sites,&self) && d==reference,"incremental landing demand");
    PathQuality compact;
    require(graph.evaluateCompact(0,{h},compact,&self) && compact.cost==graph.stepCost(0,h,&self),"pattern and maze price");
    graph.release(0);
    require(graph.demand().empty(),"residual landing resources");
}
