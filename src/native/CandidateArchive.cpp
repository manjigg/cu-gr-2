#include "CandidateArchive.h"
#include "Profile.h"
#include <numeric>

namespace native3d {
namespace {
size_t payloadOf(const ArchivedCandidate& c) {
    return sizeof(ArchivedCandidate)+sizeof(Route)+sizeof(RouteResourceSummary)+
           c.route->edges.size()*sizeof(Edge)+c.operation.size()+
           c.summary->resources().size()*sizeof(std::pair<Resource,double>)+
           c.summary->hbtSites().size()*sizeof(int);
}
}
bool equivalentRoutes(const Route& a,const Route& b) {
    return a.root==b.root && a.edges.size()==b.edges.size() &&
           std::equal(a.edges.begin(),a.edges.end(),b.edges.begin(),
                      [](const Edge& x,const Edge& y){return !(x<y) && !(y<x);});
}
bool resourceDominates(const RouteResourceSummary& a,const RouteResourceSummary& b) {
    if(!a.sameOwner(b) || !std::isfinite(a.cost()) || !std::isfinite(b.cost()) || a.cost()>b.cost()) return false;
    if(!std::includes(b.hbtSites().begin(),b.hbtSites().end(),a.hbtSites().begin(),a.hbtSites().end())) return false;
    auto other=b.resources().begin();
    for(const auto& demand:a.resources()) {
        while(other!=b.resources().end() && other->first<demand.first) ++other;
        if(other==b.resources().end() || other->first!=demand.first || !(demand.second<=other->second)) return false;
    }
    return true;
}
CandidateArchive::CandidateArchive(const RoutingGraph& graph,size_t perNet,size_t payloadLimit,bool safePruning):
    graph(graph),perNet(perNet),payloadLimit(payloadLimit),safePruning(safePruning) {
    if(!perNet || !payloadLimit) throw std::invalid_argument("Invalid candidate archive capacity");
}
const std::vector<ArchivedCandidate>& CandidateArchive::entries(int net) const {
    static const std::vector<ArchivedCandidate> empty;
    auto it=pools.find(net);
    return it==pools.end()?empty:it->second.candidates;
}
uint64_t CandidateArchive::revision(int net) const {
    auto it=pools.find(net);
    return it==pools.end()?0:it->second.revision;
}
void CandidateArchive::erase(int net) {
    const auto it=pools.find(net);
    if(it==pools.end()) return;
    for(const auto& c:it->second.candidates) payload-=payloadOf(c);
    active-=it->second.candidates.size();
    pools.erase(it);
}
bool CandidateArchive::insert(int net,Route&& route,PreparedRoute prepared,std::string op,int stage,bool related) {
    Measured measure(Timing::Archive);
    if(!prepared || prepared->netId()!=net || !graph.staticallyAvailable(*prepared)) {
        ++invalid;
        return false;
    }
    const auto& existing=entries(net);
    for(const auto& c:existing) if(equivalentRoutes(*c.route,route)) {
        ++duplicates;
        return false;
    }
    if(safePruning) {
        Measured prune(Timing::ArchivePrune);
        for(const auto& c:existing) if(resourceDominates(*c.summary,*prepared)) {
            ++dominated;
            return false;
        }
    }
    auto pending=existing;
    if(safePruning) {
        Measured prune(Timing::ArchivePrune);
        const auto end=std::remove_if(pending.begin(),pending.end(),
            [&](const ArchivedCandidate& c){return resourceDominates(*prepared,*c.summary);});
        // 只有替代者实际入档，才删除它支配的原候选。
        pending.erase(end,pending.end());
    }
    const auto safeRemoved=existing.size()-pending.size();
    const auto inserted=std::make_shared<const Route>(std::move(route));
    pending.push_back({inserted,std::move(prepared),std::move(op),stage,related});
    auto lessCost=[](const ArchivedCandidate& a,const ArchivedCandidate& b) {
        return a.summary->cost()<b.summary->cost();
    };
    std::stable_sort(pending.begin(),pending.end(),lessCost);
    std::vector<bool> selected(pending.size(),false);
    std::vector<ArchivedCandidate> keep;
    size_t nextPayload=payload;
    for(const auto& c:existing) nextPayload-=payloadOf(c);
    auto take=[&](size_t i) {
        const auto bytes=payloadOf(pending[i]);
        if(selected[i] || keep.size()==perNet || bytes>payloadLimit-nextPayload) return;
        selected[i]=true;
        keep.push_back(pending[i]);
        nextPayload+=bytes;
    };
    auto minimum=[&](auto value) {
        const auto it=std::min_element(pending.begin(),pending.end(),[&](const auto& a,const auto& b){
            return value(*a.summary)<value(*b.summary);
        });
        take(it-pending.begin());
    };
    take(0);
    minimum([](const auto& a){return a.hbtSites().size();});
    minimum([](const auto& a){return a.wirelength();});
    minimum([](const auto& a){return a.vias();});
    for(size_t i=0;i<pending.size();++i) if(pending[i].exchangeRelated) take(i);
    std::set<std::vector<int>> signatures;
    for(const auto& c:keep) signatures.insert(c.summary->hbtSites());
    for(size_t i=0;i<pending.size();++i)
        if(!selected[i] && signatures.insert(pending[i].summary->hbtSites()).second) take(i);
    for(size_t i=0;i<pending.size();++i) take(i);
    if(std::none_of(keep.begin(),keep.end(),[&](const auto& c){return c.route==inserted;})) {
        ++evicted;
        return false;
    }
    auto& pool=pools[net];
    dominated+=safeRemoved;
    evicted+=pending.size()-keep.size();
    active=active-pool.candidates.size()+keep.size();
    ++retained;
    pool.candidates.swap(keep);
    pool.revision=++nextRevision;
    payload=nextPayload;
    peak=std::max(peak,payload);
    return true;
}
Json CandidateArchive::report() const {
    return {{"archive_candidates",active},{"retained_candidates",retained},{"duplicate_candidates",duplicates},
            {"safe_pruned_candidates",dominated},{"archive_evicted_candidates",evicted},
            {"static_rejected_candidates",invalid},{"archive_payload_peak_bytes",peak},
            {"archive_payload_limit_bytes",payloadLimit}};
}
}
