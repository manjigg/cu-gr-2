#include "native/ConflictOptimizer.h"
#include "native/CandidateArchive.h"
#include "native/IncrementalRoute.h"
#include "native/TopologyExchange.h"
#include <iostream>

using namespace native3d;

static void require(bool condition,const char* reason) {
    if(!condition) throw std::logic_error(reason);
}

static void checkBudget(RoutingGraph& graph) {
    IncumbentSolution incumbent(graph);
    auto make=[&](int left,int right,bool doubleHbt,bool detour) {
        const auto l=graph.sites.at(left).transitions.front();
        const auto r=graph.sites.at(right).transitions.front();
        Route result{l.a,{l}};
        auto run=[&](GRPoint a,GRPoint b) {
            auto mid=a;
            ++mid.x;
            result.edges.emplace(a,mid,GREdgeType::WIRE);
            result.edges.emplace(mid,b,GREdgeType::WIRE);
        };
        run(l.a,r.a);
        if(doubleHbt) result.edges.insert(r);
        else if(!detour) run(l.b,r.b);
        else {
            auto a=l.b,b=r.b;
            for(const auto& p:{a,b}) {
                auto low=p;
                --low.layerIdx;
                auto moved=low;
                ++moved.y;
                auto up=moved;
                ++up.layerIdx;
                result.edges.emplace(p,low,GREdgeType::VIA);
                result.edges.emplace(low,moved,GREdgeType::WIRE);
                result.edges.emplace(moved,up,GREdgeType::VIA);
            }
            ++a.y;++b.y;
            run(a,b);
        }
        return result;
    };
    std::vector<Route> old{make(0,1,true,false),make(2,3,false,true)};
    std::vector<Route> proposals{make(0,1,false,false),make(2,3,true,false)};
    std::vector<PreparedRoute> after;
    for(int i:{0,1}) {
        auto summary=graph.prepareRoute(i,old[i]);
        incumbent.publish(i,std::move(old[i]),summary);
        after.push_back(graph.prepareRoute(i,proposals[i]));
        require(!incumbent.improves(summary,after.back(),summary),"unilateral budget/cost");
    }
    const std::vector<int> ids{0,1};
    const std::vector<unsigned> versions{incumbent.current[0].version,incumbent.current[1].version};
    const std::vector<std::vector<PreparedRoute>> pools{{incumbent.current[0].summary,after[0]},
                                                        {incumbent.current[1].summary,after[1]}};
    require(graph.available(*after[0],nullptr)==false,"same-net occupancy must be removed");
    for(int i:ids) {
        const auto view=graph.readView(i);
        require(graph.available(*after[i],&view),"budget-only interactions");
    }
    Deadline time(2);
    const auto answer=selectGroup(graph,ids,pools,time,0,3);
    const auto exhaustive=selectGroup(graph,ids,pools,time,0,3,true);
    require(answer.complete && answer.selection==std::vector<size_t>{1,1} &&
            answer.selection==exhaustive.selection,"budget exchange full enumeration");
    const auto original=graph.demand();
    require(!incumbent.replaceJoint(ids,{versions[0]+1,versions[1]},proposals,after) &&
            graph.demand()==original && incumbent.hbtTotal==3,"stale incumbent transaction");
    require(incumbent.replaceJoint(ids,versions,proposals,after) && incumbent.hbtTotal==3,"budget atomic commit");
    require(after[0]->wirelength()+after[1]->wirelength()<pools[0][0]->wirelength()+pools[1][0]->wirelength(),
            "actual shorter wires with equal HBT total");
    for(int id:ids) graph.release(id);
    require(graph.demand().empty(),"budget fixture release");
}

int main(int argc,char** argv) {
    Parameters parameters(argc,argv);
    MultiDieDesign model(parameters,argv[1]);
    model.config["enable_topology_exchange"]=true;
    RoutingGraph graph(model);
    if(model.config.value("budget_fixture",false)) {
        checkBudget(graph);
        return 0;
    }
    const auto a=graph.sites.at(0).transitions.front();
    const auto b=graph.sites.at(1).transitions.front();
    auto extend=[&](const Edge& hbt,int length) {
        Route route{hbt.a,{hbt}};
        auto p=hbt.a;
        for(int i=0;i<length;++i) {
            auto next=p;
            ++next[model.dies[p.dieIdx]->grid.getLayerDirection(p.layerIdx)];
            route.edges.emplace(p,next,GREdgeType::WIRE);
            p=next;
        }
        return route;
    };
    const auto oldA=graph.prepareRoute(0,extend(a,0));
    const auto oldB=graph.prepareRoute(1,extend(b,2));
    const auto newA=graph.prepareRoute(0,extend(b,1));
    const auto newB=graph.prepareRoute(1,extend(a,0));
    require(graph.staticallyAvailable(*newB),"static legality");
    require(newA->cost()>oldA->cost() && newB->cost()<oldB->cost(),"fixture objective");
    graph.commitPrepared(0,oldA);
    graph.commitPrepared(1,oldB);
    const auto original=graph.demand();
    const auto epoch=graph.resourceVersion();
    const auto view=graph.readView(1);
    const auto blocked=graph.dynamicBlockers(*newB,view);
    CandidateArchive archive(graph,2,16384,true);
    const auto longA=graph.prepareRoute(0,extend(a,2));
    require(resourceDominates(*oldA,*longA) && !resourceDominates(*oldA,*newA),"strict resource dominance");
    require(archive.insert(0,extend(a,2),longA,"long",2),"retain initial alternative");
    require(archive.insert(0,extend(a,0),oldA,"short",2),"retain dominating replacement");
    require(!archive.insert(0,extend(a,0),oldA,"duplicate",2),"physical duplicate");
    require(archive.insert(0,extend(b,1),newA,"different_site",2,true) && archive.entries(0).size()==2,
            "different physical site survives");
    require(archive.insert(1,extend(a,0),newB,"dynamic_conflict",2,true),"retain blocked complete candidate");
    require(archive.report().at("safe_pruned_candidates").get<int>()==1 &&
            archive.report().at("duplicate_candidates").get<int>()==1,"count exact filters separately");
    CandidateArchive small(graph,1,1,true);
    require(!small.insert(0,extend(a,0),oldA,"full",2) &&
            small.report().at("archive_evicted_candidates").get<int>()==1,"payload bound");
    require(blocked.size()==1 && blocked.front().occupied==graph.hbtResource(a.site),"actual HBT blocker");
    require(graph.users(blocked.front().occupied)==std::vector<int>{0},"actual blocker owner");
    const auto released=graph.groupView(1,{0,1});
    require(!graph.available(*newB,&view) && graph.available(*newB,&released),"group-only release");
    const std::vector<int> ids{0,1};
    const std::vector<std::vector<PreparedRoute>> pools{{oldA,newA},{oldB,newB}};
    const auto outside=graph.groupView(0,ids);
    require(!graph.canReplaceGroup(ids,{oldA,newB},outside) && !graph.canReplaceGroup(ids,{newA,oldB},outside),
            "no feasible unilateral replacement");
    for(const auto limit:{size_t(2),size_t(0)}) {
        Deadline time(1);
        const auto exhaustive=selectGroup(graph,ids,pools,time,3,limit,true);
        const auto bounded=selectGroup(graph,ids,pools,time,3,limit);
        require(exhaustive.complete && exhaustive.combinations==4 && bounded.complete,"complete finite search");
        require(exhaustive.selection==std::vector<size_t>{1,1} && bounded.selection==exhaustive.selection,
                "exact simultaneous exchange");
        require(exhaustive.bestCost==bounded.bestCost && bounded.bestExcess==5-limit,"outside HBT budget");
    }
    Deadline stopped(1);
    stopped.limitChecks(1);
    require(!selectGroup(graph,ids,pools,stopped,0,2).complete,"timeout is not exhaustion");
    require(graph.demand()==original && graph.resourceVersion()==epoch,"read-only search");
    require(!graph.replaceGroup(ids,{oldA,newB}) && graph.demand()==original,"failed transaction");
    require(graph.replaceGroup(ids,{newA,newB}),"atomic exchange");
    require(graph.users(graph.hbtResource(a.site))==std::vector<int>{1},"new owner");
    bool stale=false;
    try {graph.canReplaceGroup(ids,{newA,newB},outside);}
    catch(const std::logic_error&) {stale=true;}
    require(stale,"stale snapshot");
    graph.release(0);
    graph.release(1);
    require(graph.demand().empty(),"residual resources");

    model.config["optimization_mode"]="joint";
    for(bool archiveEnabled:{false,true}) {
        model.config["enable_topology_archive"]=archiveEnabled;
        IncumbentSolution target(graph);
        target.publish(0,extend(a,2),longA);
        target.current[0].alternatives.push_back(extend(a,0));
        Deadline stage(2);
        std::map<std::string,TopologyStats> stats;
        const auto result=runTopologyExchange(target,{{0.,0}}, {},stage,1e-9,1,".",stats);
        require(result.accepted==1 && target.current[0].route.edges.size()==1,
                "apply the Stage 1 alternative even when fresh search reaches its local deadline");
        graph.release(0);
    }

    model.config["bonding"]["min_spacing"]=1500;
    RoutingGraph spaced(model);
    auto initial=spaced.prepareRoute(0,extend(a,0));
    auto adjacent=spaced.prepareRoute(1,extend(b,0));
    require(spaced.staticallyAvailable(*adjacent),"unoccupied adjacent site is statically legal");
    spaced.commitPrepared(0,initial);
    const auto spacing=spaced.dynamicBlockers(*adjacent,spaced.readView(1));
    require(spacing.size()==1 && spacing.front().requested==spaced.hbtResource(b.site) &&
            spacing.front().occupied==spaced.hbtResource(a.site),"retain both HBT identities in a spacing conflict");
    const auto spacingView=spaced.groupView(1,ids);
    require(spaced.available(*adjacent,&spacingView) &&
            !spaced.canReplaceGroup(ids,{initial,adjacent},spaced.groupView(ids.front(),ids)),
            "legal individual HBTs can violate joint spacing");
    spaced.release(0);

    model.config["bonding"]["min_spacing"]=1000;
    for(auto& die:model.config["dies"]) die["via_capacity"]=1;
    RoutingGraph vias(model);
    auto withVia=[&](const Edge& h) {
        auto r=extend(h,0);
        auto upper=h.a;
        ++upper.layerIdx;
        r.edges.emplace(h.a,upper,GREdgeType::VIA);
        return r;
    };
    auto firstVia=vias.prepareRoute(0,withVia(a)),secondVia=vias.prepareRoute(1,withVia(b));
    vias.commitPrepared(0,firstVia);
    const auto viaBlocks=vias.dynamicBlockers(*secondVia,vias.readView(1));
    require(std::any_of(viaBlocks.begin(),viaBlocks.end(),[&](const BlockedResource& block){
        return block.requested==block.occupied && vias.location(block.occupied).type==GREdgeType::VIA;
    }),"capture actual via capacity blocker from complete route");
    const auto viaView=vias.groupView(1,ids);
    require(vias.available(*secondVia,&viaView) &&
            !vias.canReplaceGroup(ids,{firstVia,secondVia},vias.groupView(ids.front(),ids)),
            "validate summed resources for distinct sites sharing via capacity");
    vias.release(0);
    std::cout<<"physical HBT exchange, exact search and atomic commit passed\n";
}
