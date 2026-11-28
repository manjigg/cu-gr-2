#pragma once
#include "PatternSearch.h"

namespace native3d {
struct NetRoute {
    Route route{{0,0,0},{}};
    PreparedRoute summary;
    Routes alternatives;
    FastPatternCache pattern;
    unsigned version=0,failures=0;
};
struct IncumbentSolution {
    std::vector<NetRoute> current;
    RoutingGraph& graph;
    explicit IncumbentSolution(RoutingGraph& g);
    const std::string mode;
    bool protect;
    size_t hbtTotal=0,budget=std::numeric_limits<size_t>::max();
    size_t excess(size_t total) const {return total>budget?total-budget:0;}
    bool improves(const PreparedRoute& old,const PreparedRoute& candidate,const PreparedRoute& best) const;
    size_t after(const PreparedRoute& old,const PreparedRoute& candidate) const;
    void publish(int net,Route&& route,PreparedRoute summary);
    bool replace(int net,unsigned version,Route&& route,PreparedRoute summary);
    bool replaceJoint(const std::vector<int>&,const std::vector<unsigned>&,std::vector<Route>&,
                      const std::vector<PreparedRoute>&);
    Json metrics() const;
};
struct IncrementalResult {
    Routes routes;
    std::vector<int> unrouted;
};
IncrementalResult routeIncrementally(RoutingGraph&,const std::vector<int>& stages,Json& report,const std::string& outputDirectory);
}
