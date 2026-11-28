#include "native/FailureRepair.h"
#include <iostream>
using namespace native3d;

static int checkCumulative(IncumbentSolution& solution) {
    auto& graph=solution.graph;
    std::vector<Route> routes;
    std::vector<PreparedRoute> summaries;
    for(int id:{0,1,3}) {
        const auto edge=graph.sites.at(id).transitions.front();
        routes.push_back({edge.a,{edge}});
        summaries.push_back(graph.prepareRoute(summaries.size(),routes.back()));
    }
    for(int a=0;a<3;++a) for(int b=a+1;b<3;++b) {
        if(!graph.replaceGroup({a,b},{summaries[a],summaries[b]})) return 1;
        graph.release(a);
        graph.release(b);
    }
    solution.publish(0,Route(routes[0]),summaries[0]);
    const auto view=graph.readView(0);
    const auto version=solution.current[0].version;
    const auto snapshot=graph.demand();
    if(graph.replaceGroup({0,1,2},summaries) || graph.demand()!=snapshot ||
       solution.current[0].version!=version || graph.users(graph.hbtResource(0))!=std::vector<int>{0})
        return 1;
    if(!solution.replace(0,version,Route(routes[0]),summaries[0]) ||
       solution.replace(0,version,Route(routes[0]),summaries[0]) ||
       solution.current[0].version!=version+1 || graph.demand()!=snapshot)
        return 1;
    try {
        graph.available(*summaries[0],&view);
        return 1;
    } catch(const std::logic_error&) {
        graph.release(0);
    }
    return !graph.demand().empty() || !graph.users(graph.hbtResource(0)).empty();
}

int main(int argc,char** argv) {
    Parameters params(argc,argv);MultiDieDesign model(params,argv[1]);RoutingGraph graph(model);
    IncumbentSolution solution(graph);
    if(model.nets.size()==3) return checkCumulative(solution);
    const Edge first=graph.sites.at(0).transitions.at(0), alternate=graph.sites.at(1).transitions.at(0),
               shared=graph.sites.at(0).transitions.at(1);
    Route initial{first.a,{first}}, urgent{shared.a,{shared}};
    auto old=graph.prepareRoute(0,initial),newNet=graph.prepareRoute(1,urgent);
    solution.publish(0,std::move(initial),old);
    auto snapshot=graph.demand();
    if(graph.users(graph.hbtResource(0))!=std::vector<int>{0}) return 1;
    if(graph.replaceGroup({0,1},{old,newNet}) || graph.demand()!=snapshot) return 1;
    if(graph.users(graph.hbtResource(0))!=std::vector<int>{0}) return 1;
    auto view=graph.groupView(0,{0,1},{newNet});
    if(graph.available(*old,&view) || !graph.available(0,{alternate},&view)) return 1;
    Deadline time(5);
    const auto stats=repairFailedNets(solution,{{0.,1}},time,1.,1);
    std::cout<<stats.dump()<<std::endl;
    if(stats.at("groups_published").get<int>()!=1 || solution.hbtTotal!=2 ||
       graph.users(graph.hbtResource(0))!=std::vector<int>{1} || graph.users(graph.hbtResource(1))!=std::vector<int>{0}) return 1;
    for(int id:{0,1}) graph.release(id);
    if(!graph.demand().empty() || !graph.users(graph.hbtResource(0)).empty() || !graph.users(graph.hbtResource(1)).empty()) return 1;
    return 0;
}
