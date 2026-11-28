#include "native/PatternSearch.h"
#include "native/Profile.h"
#include "gr/PatternRoute.h"
#include <random>

using namespace native3d;
int main(int argc,char** argv) {
    Parameters params(argc,argv);
    MultiDieDesign model(params,argv[1]);RoutingGraph graph(model);
    std::mt19937 random(93);
    std::vector<Edge> local;
    for(int die:{0,1}) for(int x:{0,1}) for(int y:{0,1}) for(int l:{1,2})
        for(const auto& edge:graph.neighbors({die,l,x,y},0)) local.push_back(edge);
    for(unsigned i=0;i<1000;++i) {
        Edges parent,combined;
        for(int j=0;j<int(i%13);++j) parent.insert(local[random()%local.size()]);
        combined=parent;
        for(int j=0;j<int(i%7);++j) combined.insert(local[random()%local.size()]);
        std::vector<Edge> ordered(parent.begin(),parent.end()),edges(combined.begin(),combined.end());
        PathQuality ref,test;
        Demand demand;
        const bool a=graph.evaluateCompact(0,edges,ref);
        const bool b=graph.evaluateIncrement(0,ordered,graph.resources(parent),edges,demand,test);
        if(a!=b || (a && (ref.cost!=test.cost || ref.wire!=test.wire || ref.vias!=test.vias ||
                           ref.hbt!=test.hbt || demand!=graph.resources(combined)))) return 1;
    }
    PatternRoute::readFluteLUT();
#ifdef CUGR_DP_MEMORY_PROFILE
    Profile profile;
#endif
    model.config["routing_mode"]="fast";
    Deadline time(60);
    FastPatternCache cache;
    std::vector<std::vector<Edges>> pools;
    for(bool incremental:{false,true}) {
        model.config["enable_incremental_dp"]=incremental;
        std::vector<Edges> pool;
        for(const auto& tree:PatternSearch(graph,0,time).enumerateFast(false,cache)) pool.push_back(tree.edges);
#ifdef CUGR_DP_MEMORY_PROFILE
        if(profile.liveLabelBytes()!=0 || profile.peakLiveLabelBytes()==0) return 1;
#endif
        pools.push_back(std::move(pool));
    }
    if(pools[0].empty() || pools[0]<pools[1] || pools[1]<pools[0]) return 1;
    return 0;
}
