#include "native/SparseMaze.h"
#include "native/BranchRepair.h"
#include "gr/PatternRoute.h"
using namespace native3d;
int main(int argc,char** argv) {
    if(argc!=2 && argc!=3) return 2;
    Parameters p(argc,argv);
    MultiDieDesign model(p,argv[1]);
    RoutingGraph graph(model);
    Deadline time(45);
    if(argc==3) {
        if(std::string(argv[2])!="--branch-test") return 2;
        GRPoint a(0,2,0,0),b(0,2,1,0);
        Route blocked{a,{Edge(a,b,GREdgeType::WIRE)}};
        if(graph.available(0,blocked.edges)) return 1;
        const auto repaired=repairLocalBranches(graph,0,blocked,{a,b},time);
        if(repaired.empty() || !graph.available(0,repaired.front().edges)) return 1;
        int vias=0,wires=0;
        std::map<GRPoint,std::vector<GRPoint>> adj;
        for(const auto& e:repaired.front().edges) {
            vias+=e.type==GREdgeType::VIA; wires+=e.type==GREdgeType::WIRE;
            adj[e.a].push_back(e.b);adj[e.b].push_back(e.a);
        }
        std::set<GRPoint> seen{a};std::vector<GRPoint> todo{a};
        while(!todo.empty()) {
            auto p=todo.back();todo.pop_back();
            for(auto q:adj[p]) if(seen.insert(q).second) todo.push_back(q);
        }
        if(!seen.count(b) || vias<2 || wires<1 || seen.size()!=repaired.front().edges.size()+1) return 1;
        return 0;
    }
    PatternRoute::readFluteLUT();
    std::vector<Routes> pool;
    for(int n=0;n<int(model.nets.size());++n) pool.push_back(PatternSearch(graph,n,time).enumerate(false));
    const auto best=selectRoutes(graph,pool,time);
    if(best.status!="OPTIMAL" || best.objective!=8 || best.lower!=8) return 1;
    for(int n=0;n<2;++n) graph.commit(n,pool[n].at(best.selection[n]));
    graph.captureCongestion();
    graph.release(1);
    const auto alternatives=PatternSearch(graph,1,time).enumerate(true);
    if(alternatives.empty()) return 1;
    for(const auto& route:alternatives) graph.validate(1,route);
    auto maze=SparseMaze(graph,1,time).candidates(4,false);
    if(maze.empty()) return 1;
    for(const auto& r:maze) graph.validate(1,r);
    graph.commit(1,maze.front());
    graph.release(1);
    graph.release(0);
    if(!graph.demand().empty()) return 1;
    for(auto& candidates:pool)
        candidates.erase(std::remove_if(candidates.begin(),candidates.end(),[](const Route& r) {
            for(const auto& e:r.edges) if(e.type==GREdgeType::HBT && e.site!=0) return true;
            return false;
        }),candidates.end());
    if(selectRoutes(graph,pool,time).status!="INFEASIBLE") return 1;
    std::cout<<"PASS joint labels, layer selection and exact two-net resource master\n";
}
