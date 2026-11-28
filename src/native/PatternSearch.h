#pragma once
#include "RoutingGraph.h"
#include <chrono>
#include <atomic>
namespace native3d {
struct SearchTimeout : std::runtime_error {
    SearchTimeout():std::runtime_error("Search time limit reached") {}
};
class Deadline {
    std::chrono::steady_clock::time_point end;
    uint64_t workLimit=0;
    mutable uint64_t consumed=0;
public:
    explicit Deadline(double seconds);
    Deadline(const Deadline& stage,double seconds,const std::atomic<bool>* cancel=nullptr);
    bool expired() const;
    double overrun() const;
    // 可重复实验按现有协作检查点限制工作，同时保留真实时钟的保护截止时间。
    void limitChecks(uint64_t count) {workLimit=count;consumed=0;}
    uint64_t checks() const {return consumed;}
private:
    const std::atomic<bool>* cancelled=nullptr;
public:
    void check() const;
};
using Routes=std::vector<Route>;
struct ProjectedTopology;
struct FastPatternCache {
    std::shared_ptr<const ProjectedTopology> topology;
};
class PatternSearch {
public:
    PatternSearch(const RoutingGraph& graph,int net,const Deadline& time):g(graph),net(net),time(time) {}
    Routes enumerate(bool detours) const;
    Routes enumerateFast(bool detours,FastPatternCache& cache) const;
    Routes localSeed() const;
    Routes localSeed(const std::vector<std::vector<GRPoint>>& pins) const;
    Routes refineMaze(const Route&,const ResourceReadView* view=nullptr) const;
    std::set<Edges> localPaths(GRPoint a,GRPoint b,const ResourceReadView* view=nullptr) const;
private:
    using PathList=std::set<Edges>;
    using PathCache=std::map<std::pair<GRPoint,GRPoint>,std::shared_ptr<const PathList>>;
    const RoutingGraph& g;
    int net;
    const Deadline& time;
    std::set<Edges> paths(GRPoint a,GRPoint b,bool detours,PathCache* cache=nullptr,bool fast=false) const;
};
struct MasterResult {
    std::string status;
    double objective=std::numeric_limits<double>::infinity(), lower=0;
    std::vector<unsigned> selection;
};
MasterResult selectRoutes(RoutingGraph&,const std::vector<Routes>&,const Deadline&,const std::vector<unsigned>& feasible = {});
}
