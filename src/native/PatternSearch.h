#pragma once
#include "RoutingGraph.h"
#include <chrono>
namespace native3d {
struct SearchTimeout : std::runtime_error {
    SearchTimeout():std::runtime_error("Search time limit reached") {}
};
class Deadline {
    std::chrono::steady_clock::time_point end;
public:
    explicit Deadline(double seconds);
    void check() const;
};
using Routes=std::vector<Route>;
class PatternSearch {
public:
    PatternSearch(const RoutingGraph& graph,int net,const Deadline& time):g(graph),net(net),time(time) {}
    Routes enumerate(bool detours) const;
    Routes localSeed() const;
    Routes localSeed(const std::vector<std::vector<GRPoint>>& pins) const;
    Routes refineMaze(const Route&) const;
    std::set<Edges> localPaths(GRPoint a,GRPoint b) const;
private:
    const RoutingGraph& g;
    int net;
    const Deadline& time;
    std::set<Edges> paths(GRPoint a,GRPoint b,bool detours) const;
};
struct MasterResult {
    std::string status;
    double objective=std::numeric_limits<double>::infinity(), lower=0;
    std::vector<unsigned> selection;
};
MasterResult selectRoutes(RoutingGraph&,const std::vector<Routes>&,const Deadline&,const std::vector<unsigned>& feasible = {});
}
