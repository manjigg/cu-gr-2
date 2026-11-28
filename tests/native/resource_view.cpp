#include "native/SparseMaze.h"
#include <future>
#include <iostream>

using namespace native3d;

static void require(bool ok,const char* message) {
    if(!ok) throw std::logic_error(message);
}
static std::vector<double> prices(const RoutingGraph& g,int net,const ResourceReadView* view) {
    std::vector<double> result;
    for(int die:{0,1}) {
        const auto& grid=g.model.dies[die]->grid;
        for(unsigned layer=0;layer<grid.getNumLayers();++layer)
            for(unsigned x=0;x<grid.getSize(0);++x) for(unsigned y=0;y<grid.getSize(1);++y)
                for(const auto& e:g.neighbors({die,int(layer),int(x),int(y)},net)) {
                    result.push_back(g.stepCost(net,e,view));
                    result.push_back(g.available(net,{e},view));
                }
    }
    return result;
}
int main(int argc,char** argv) {
    Parameters p(argc,argv);
    MultiDieDesign model(p,argv[1]);
    model.config["bonding"]["sites"][0]["fixed_net"]=0;
    model.config["dies"][0]["via_capacity"]=1;
    RoutingGraph graph(model);
    const Edge site=graph.sites.at(0).transitions.front();
    const Edge alternate=graph.sites.at(1).transitions.front();
    const auto landing=site.a;
    GRPoint raised=landing;
    ++raised.layerIdx;
    GRPoint end=raised;
    ++end[model.dies[raised.dieIdx]->grid.getLayerDirection(raised.layerIdx)];
    Route tree{landing,{site,Edge(landing,raised,GREdgeType::VIA),Edge(raised,end,GREdgeType::WIRE)}};
    auto old=graph.prepareRoute(0,tree);
    graph.commitPrepared(0,old);
    const auto snapshot=graph.demand();
    auto self=graph.readView(0);
    auto other=graph.readView(1);
    require(graph.available(*old,&self) && !graph.available(*old),"logical release");
    require(!graph.available(1,{site},&other),"fixed owner");
    require(!graph.available(1,{Edge(landing,raised,GREdgeType::VIA)},&other),"foreign via");
    auto worker=std::async(std::launch::async,[&]{return prices(graph,0,&self);});
    auto foreign=prices(graph,1,&other);
    auto logical=worker.get();
    require(foreign==prices(graph,1,nullptr),"unrouted net view");
    std::vector<Edge> edges(tree.edges.begin(),tree.edges.end());
    PathQuality quality;
    require(graph.evaluateCompact(0,edges,quality,&self),"compact validation");
    Edges extended;
    Demand delta;
    std::vector<int> sites;
    require(graph.extendPath(0,tree.edges,extended,delta,sites,&self),"extend view");
    require(delta==graph.resources(tree.edges) && sites==old->hbtSites(),"via footprint");
    graph.validate(0,tree,&self);
    bool rejected=false;
    try {graph.validate(0,{alternate.a,{alternate}},&self);}
    catch(const std::invalid_argument&) {rejected=true;}
    require(rejected,"fixed HBT obligation");
    require(graph.demand()==snapshot,"search mutated ledger");
    graph.release(0);
    require(prices(graph,0,nullptr)==logical,"view differs from actual serial release");
    rejected=false;
    try {graph.available(*old,&self);} catch(const std::logic_error&) {rejected=true;}
    require(rejected,"stale epoch");
    require(graph.demand().empty(),"residual demand");
    model.config["bonding"]["min_spacing"]=1500;
    RoutingGraph spaced(model);
    auto unfunded=spaced.readView(1);
    require(!spaced.available(1,{alternate},&unfunded),"spacing to unoccupied fixed site");
    spaced.commit(0,{site.a,{site}});
    auto ownFixed=spaced.readView(0);
    require(spaced.available(0,{site},&ownFixed),"owner fixed landing");
    require(!spaced.available(0,{alternate},&ownFixed),"spacing to logically released fixed site");
    spaced.release(0);
    std::cout<<"PASS serial-release equivalence for every neighbor, fixed ownership and snapshot lifetime\n";
}
