#include "native/TopologyRefinement.h"
#include "native/SeedTopology.h"
#include "gr/PatternRoute.h"
#include <iostream>

using namespace native3d;
static bool checkMigration(RoutingGraph& graph,const Deadline& time) {
    // 原分叉在 metal4。它向两侧终点走两条绕路，第三臂接到下方真实引脚。
    const GRPoint junction(0,3,1,1),target(0,4,1,1);
    Route route{{0,4,0,1},{}};
    auto addPath=[&](const std::vector<GRPoint>& path) {
        for(size_t i=1;i<path.size();++i)
            route.edges.emplace(path[i-1],path[i],path[i-1].layerIdx==path[i].layerIdx?GREdgeType::WIRE:GREdgeType::VIA);
    };
    addPath({junction,{0,2,1,1},{0,2,0,1},{0,3,0,1},route.root});
    addPath({junction,{0,3,1,2},{0,4,1,2},{0,4,2,2},{0,3,2,2},{0,3,2,1},{0,4,2,1}});
    addPath({junction,{0,3,1,0}});
    auto previous=graph.prepareRoute(0,route);
    auto degree=[](const Edges& edges,GRPoint p) {
        return std::count_if(edges.begin(),edges.end(),[&](const Edge& edge){return edge.a==p || edge.b==p;});
    };
    std::map<std::string,TopologyStats> stats;
    for(const auto& alternative:refineTopology(graph,0,route,time,stats)) {
        const auto prepared=graph.prepareRoute(0,alternative.route);
        if(degree(alternative.route.edges,target)==3 && degree(alternative.route.edges,junction)<3 &&
           prepared->cost()<previous->cost()) {
            graph.commitPrepared(0,prepared);
            graph.release(0);
            return graph.demand().empty();
        }
    }
    std::cerr<<stats["steiner_migration"].report().dump()<<'\n';
    return false;
}

int main(int argc,char** argv) {
    Parameters params(argc,argv);
    MultiDieDesign model(params,argv[1]);
    model.config["optimization_mode"]="joint";
    model.config["routing_mode"]="fast";
    model.config["topology_attempt_limit"]=100;
    RoutingGraph graph(model);
    Deadline time(20);
    PatternRoute::readFluteLUT();
    if(model.config.value("migration_fixture",false)) return !checkMigration(graph,time);
    auto seeds=initialCandidates(graph,0,time);
    if(seeds.empty()) return 1;
    model.config["enable_blocked_prim"]=true;model.config["prim_min_pins"]=3;
    model.config["enable_incremental_seed_sites"]=true;
    auto fast=initialCandidates(graph,0,time);
    if(seeds.size()!=fast.size()) return 1;
    for(size_t i=0;i<fast.size();++i)
        if(seeds[i].root!=fast[i].root || seeds[i].edges<fast[i].edges || fast[i].edges<seeds[i].edges) return 1;
    for(const std::string op:{"merge","relocation","augmentation","steiner_migration","edge_reconnection"}) {
        for(const std::string name:{"merge","relocation","augmentation","steiner_migration","edge_reconnection"})
            model.config[(name=="steiner_migration" || name=="edge_reconnection"?"enable_":"enable_hbt_")+name]=(name==op);
        std::map<std::string,TopologyStats> stats;
        size_t feasible=0;
        for(const auto& seed:seeds) {
            auto old=graph.prepareRoute(0,seed);
            for(const auto& alt:refineTopology(graph,0,seed,time,stats)) {
                const auto candidate=graph.prepareRoute(0,alt.route);
                if(alt.operation!=op || !graph.available(*candidate)) return 1;
                if(op=="augmentation" && candidate->hbtSites().size()<=old->hbtSites().size()) return 1;
                if(op=="merge" && candidate->hbtSites().size()>=old->hbtSites().size()) return 1;
                graph.commitPrepared(0,candidate);graph.release(0);
                ++feasible;
            }
        }
        std::cout<<op<<" feasible="<<feasible<<" "<<stats[op].report().dump()<<std::endl;
        if((op=="augmentation" || op=="merge" || op=="edge_reconnection") && !feasible) return 1;
    }
    return !graph.demand().empty();
}
