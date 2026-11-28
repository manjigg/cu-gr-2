#pragma once
#include "PatternSearch.h"

namespace native3d {
struct ArchivedCandidate {
    std::shared_ptr<const Route> route;
    PreparedRoute summary;
    std::string operation;
    int stage;
    bool exchangeRelated=false;
};

bool equivalentRoutes(const Route&,const Route&);
bool resourceDominates(const RouteResourceSummary&,const RouteResourceSummary&);

// 当前已提交树由 IncumbentSolution 持有。档案仅限制备选的显式数据载荷。
class CandidateArchive {
public:
    CandidateArchive(const RoutingGraph&,size_t perNet,size_t payloadLimit,bool safePruning);
    bool insert(int net,Route&&,PreparedRoute,std::string operation,int stage,bool exchangeRelated=false);
    const std::vector<ArchivedCandidate>& entries(int net) const;
    uint64_t revision(int net) const;
    void erase(int net);
    Json report() const;
private:
    struct Pool {
        std::vector<ArchivedCandidate> candidates;
        uint64_t revision=0;
    };
    const RoutingGraph& graph;
    const size_t perNet,payloadLimit;
    const bool safePruning;
    std::map<int,Pool> pools;
    size_t payload=0,peak=0,duplicates=0,dominated=0,evicted=0,invalid=0,retained=0,active=0;
    uint64_t nextRevision=0;
};
}
