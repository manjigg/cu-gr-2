#include "native/RoutingGraph.h"
#include "native/MedianRegion.h"
#include <iostream>
using namespace native3d;
static void check(bool b) { if(!b) throw std::logic_error("resource regression"); }
template<class F> void rejects(F f) {
    try { f(); } catch(const std::logic_error&) { return; }
    throw std::logic_error("expected rejection");
}
int main(int argc,char** argv) {
    if(argc!=2) return 2;
    std::vector<XY> pts{{{0,8}},{{8,0}}};
    auto plateau=weightedMedianRegion(pts,{1,1});
    check(plateau.low==XY({{0,0}}) && plateau.high==XY({{8,8}}));
    auto heavy=weightedMedianRegion(pts,{2,1});
    check(heavy.low==pts.front() && heavy.high==pts.front());
    Parameters params(argc,argv);
    MultiDieDesign model(params,argv[1]);
    RoutingGraph graph(model);
    for(int d:{0,1}) {
        const auto& grid=model.dies[d]->grid;
        for(int l=0;l<int(grid.getNumLayers());++l) for(int x=0;x<int(grid.getSize(0));++x)
            for(int y=0;y<int(grid.getSize(1));++y) for(const auto& e:graph.neighbors({d,l,x,y},0)) {
                double price=graph.stepCost(0,e);
                check(std::isfinite(price)==graph.available(0,{e}));
                if(std::isfinite(price)) check(std::abs(price-graph.cost({e}))<1e-9);
            }
    }
    auto edge=graph.sites.at(0).transitions.at(0);
    check(edge.a!=edge.b && edge.a.dieIdx==0 && edge.b.dieIdx==1);
    check(graph.sites.at(0).local[1][0]==11100);
    check(graph.sites.at(2).transitions.empty());
    auto extra=graph.sites.at(1).transitions.at(0);
    check(edge.a==extra.a && edge.b==extra.b);
    Route a{edge.a,{edge}}, b{extra.a,{extra}};
    graph.commit(0,a);
    auto before=graph.demand();
    rejects([&]{graph.commit(0,a);});
    rejects([&]{graph.commit(1,a);});
    rejects([&]{graph.release(1);});
    check(before==graph.demand());
    graph.commit(1,b); graph.release(1); graph.release(0);
    check(graph.demand().empty());
    Route stack=a;
    stack.edges.emplace(edge.a,GRPoint(0,2,edge.a.x,edge.a.y),GREdgeType::VIA);
    graph.commit(0,stack);graph.release(0);
    stack.edges.emplace(edge.a,GRPoint(0,3,edge.a.x,edge.a.y),GREdgeType::VIA);
    rejects([&]{graph.commit(0,stack);});
    check(graph.demand().empty());
    auto dup=a;dup.edges.insert(graph.sites.at(0).transitions.at(1));
    rejects([&]{graph.commit(0,dup);});
    auto disconnected=a;disconnected.edges.clear();
    rejects([&]{graph.commit(0,disconnected);});
    std::cout<<"PASS typed HBT, grid transforms, keepout, site ownership, via stack and rollback\n";
}
