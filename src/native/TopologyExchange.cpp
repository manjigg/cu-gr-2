#include "TopologyExchange.h"
#include "CandidateArchive.h"
#include "ConflictOptimizer.h"
#include "Profile.h"
#include "SparseMaze.h"
#include <fstream>

namespace native3d {
namespace {
using Clock=std::chrono::steady_clock;
constexpr long double tolerance=1e-9L;
size_t positive(const Json& cfg,const char* key,int fallback) {
    const int n=integer(cfg.value(key,Json(fallback)));
    if(n<=0) throw std::invalid_argument(std::string("Invalid ")+key);
    return n;
}
struct BlockingEvidence {
    int requester,owner;
    BlockedResource resource;
    uint64_t observedVersion;
    bool completeCandidate;
};
struct ExchangeTask {
    std::vector<BlockingEvidence> blocks;
    bool inherited=false,budget=false;
};
struct TaskVersion {
    uint64_t ledger;
    std::vector<std::pair<unsigned,uint64_t>> candidates;
    bool operator==(const TaskVersion& b) const {return ledger==b.ledger && candidates==b.candidates;}
};

Json routeRecord(const Route& route,const PreparedRoute& summary) {
    auto point=[](const GRPoint& p){return Json::array({p.dieIdx,p.layerIdx,p.x,p.y});};
    Json edges=Json::array();
    for(const auto& edge:route.edges) edges.push_back({{"from",point(edge.a)},{"to",point(edge.b)},
                                                     {"type",static_cast<int>(edge.type)},{"site",edge.site}});
    Json demands=Json::array();
    for(const auto& entry:summary->resources()) demands.push_back(Json::array({entry.first,entry.second}));
    return {{"root",point(route.root)},{"edges",edges},{"demand",demands},
            {"cost",summary->cost()},{"wire_um",summary->wirelength()},{"via",summary->vias()},
            {"hbt",summary->hbtSites()}};
}

class ExchangeStage {
public:
    ExchangeStage(IncumbentSolution& incumbent,const Deadline& deadline,double seconds,int stride,
                  const std::vector<std::vector<int>>& existing,std::map<std::string,TopologyStats>& stats):
        incumbent(incumbent),graph(incumbent.graph),cfg(graph.model.config),deadline(deadline),
        netSeconds(seconds),stride(stride),existing(existing),topologyStats(stats),
        archive(graph,positive(cfg,"topology_archive_per_net",8),
                positive(cfg,"topology_archive_payload_kib",65536)*1024,cfg.value("enable_topology_safe_pruning",false)),
        keepArchive(cfg.value("enable_topology_archive",false)),recordBlocks(cfg.value("enable_topology_exchange",false)),
        regenerate(cfg.value("enable_topology_group_regeneration",false)),
        budgetPairs(incumbent.mode=="budgeted" && cfg.value("enable_topology_budget_exchange",false)),
        taskLimit(positive(cfg,"topology_exchange_task_limit",1024)),
        blockerLimit(positive(cfg,"topology_blocker_limit",32)),
        regenerationLimit(integer(cfg.value("group_regeneration_limit",Json(32)))),
        membership(incumbent.current.size(),existing.size()) {
        if(regenerationLimit<0) throw std::invalid_argument("Invalid regeneration budget");
        for(size_t k=0;k<existing.size();++k) for(int id:existing[k]) membership.at(id)=k;
    }
    CoordinatedStage run(const std::vector<std::pair<double,int>>& priority,const std::string& output);

private:
    IncumbentSolution& incumbent;
    RoutingGraph& graph;
    const Json& cfg;
    const Deadline& deadline;
    double netSeconds;
    int stride;
    const std::vector<std::vector<int>>& existing;
    std::map<std::string,TopologyStats>& topologyStats;
    CandidateArchive archive;
    bool keepArchive,recordBlocks,regenerate,budgetPairs;
    size_t taskLimit,blockerLimit;
    int regenerationLimit;
    std::vector<size_t> membership;
    std::set<int> imported;
    std::map<std::vector<int>,ExchangeTask> pending;
    std::map<std::vector<int>,TaskVersion> completedVersions;
    std::map<int,std::set<std::pair<long double,int>>> offers;
    std::map<int,std::vector<std::pair<int,long double>>> netOffers;
    std::set<Resource> observedResources;
    size_t groups=0,attempts=0,completed=0,timeouts=0,accepted=0,regenerated=0,regenerationGroups=0;
    size_t combinations=0,nodes=0,pruned=0,taskDropped=0,blockerDropped=0,stale=0,unchanged=0;
    size_t simultaneous=0,strictPairs=0,evidenceErrors=0,invalidAlternatives=0;
    size_t fromBlocking=0,fromBudget=0;
    double deltaWire=0,deltaVia=0,deltaHbt=0,deltaCost=0,exchangeSeconds=0;
    Json cases=Json::array();
    CoordinatedStage result;

    void import(int id);
    void generate(int id);
    void solveNext();
    void refreshBudget(int id);
    void schedule(std::vector<int> ids,bool inherited,bool budget,const BlockingEvidence* evidence=nullptr);
    bool appendCase(const std::vector<int>&,const std::vector<Route>&,const std::vector<PreparedRoute>&,
                    const ExchangeTask&,const LocalResult&);
};

void ExchangeStage::import(int id) {
    if(!keepArchive || !imported.insert(id).second) return;
    auto& old=incumbent.current[id];
    for(auto& alternative:old.alternatives) {
        if(equivalentRoutes(old.route,alternative)) continue;
        PreparedRoute prepared;
        try {prepared=graph.prepareRoute(id,alternative);}
        catch(const std::invalid_argument&) {++invalidAlternatives;continue;}
        archive.insert(id,std::move(alternative),prepared,"seed_or_legacy_group",1);
    }
    old.alternatives.clear();
}
void ExchangeStage::schedule(std::vector<int> ids,bool inherited,bool budget,const BlockingEvidence* evidence) {
    Measured measure(Timing::ExchangeTasks);
    std::sort(ids.begin(),ids.end());
    if(ids.size()<2 || std::adjacent_find(ids.begin(),ids.end())!=ids.end()) return;
    auto it=pending.find(ids);
    if(it==pending.end()) {
        if(groups>=taskLimit) {++taskDropped;return;}
        it=pending.emplace(std::move(ids),ExchangeTask{}).first;
        ++groups;
    }
    auto& task=it->second;
    task.inherited|=inherited;
    task.budget|=budget;
    if(evidence && task.blocks.size()<blockerLimit &&
       std::none_of(task.blocks.begin(),task.blocks.end(),[&](const auto& b) {
           return b.requester==evidence->requester && b.owner==evidence->owner &&
                  b.resource.requested==evidence->resource.requested && b.resource.occupied==evidence->resource.occupied;
       })) task.blocks.push_back(*evidence);
}
void ExchangeStage::refreshBudget(int id) {
    if(!budgetPairs) return;
    for(const auto& old:netOffers[id]) {
        auto& bin=offers.at(old.first);
        bin.erase({old.second,id});
        if(bin.empty()) offers.erase(old.first);
    }
    netOffers[id].clear();
    const auto& old=incumbent.current[id].summary;
    if(!old) return;
    std::map<int,long double> proposed;
    for(const auto& candidate:archive.entries(id)) {
        const int delta=int(candidate.summary->hbtSites().size())-int(old->hbtSites().size());
        if(!delta) continue;
        const long double cost=static_cast<long double>(candidate.summary->cost())-old->cost();
        const auto position=proposed.emplace(delta,cost);
        if(!position.second) position.first->second=std::min(position.first->second,cost);
    }
    for(const auto& entry:proposed) {
        const auto delta=entry.first;
        for(const auto& bin:offers) {
            if((delta>0)==(bin.first>0)) continue;
            const int64_t total=int64_t(incumbent.hbtTotal)+delta+bin.first;
            if(total<0 || incumbent.excess(size_t(total))>incumbent.excess(incumbent.hbtTotal)) continue;
            // 按真实固定代价变化排序，每个数量桶至多取一个有严格改进潜力的伙伴。
            for(const auto& offer:bin.second) {
                if(offer.second==id) continue;
                if(incumbent.excess(size_t(total))<incumbent.excess(incumbent.hbtTotal) ||
                   offer.first+entry.second < -tolerance) schedule({id,offer.second},false,true);
                break;
            }
        }
        offers[delta].insert({entry.second,id});
        netOffers[id].push_back(entry);
    }
}

void ExchangeStage::generate(int id) {
    Measured generation(Timing::Candidates);
    Deadline one(deadline,netSeconds);
    ++result.attempted;
    auto& old=incumbent.current[id];
    import(id);
    std::vector<TopologyCandidate> candidates;
    if(!keepArchive) {
        // Stage 1 alternatives were eligible for the V3 single-net update as well.
        for(auto& route:old.alternatives) candidates.push_back({"seed_or_legacy_group",std::move(route)});
        old.alternatives.clear();
    }
    std::set<BlockedResource> blockers;
    auto collect=[&](const std::vector<BlockedResource>& entries) {
        Measured measure(Timing::Blockers);
        for(const auto& entry:entries) {
            if(blockers.count(entry)) continue;
            if(blockers.size()==blockerLimit) {++blockerDropped;break;}
            blockers.insert(entry);
        }
    };
    const auto observedEpoch=graph.resourceVersion();
    const auto view=graph.readView(id);
    std::function<void(const Edges&)> observe;
    if(recordBlocks) observe=[&](const Edges& edges) {
        if(blockers.size()<blockerLimit) collect(graph.dynamicBlockers(id,edges,view));
        else ++blockerDropped;
    };
    if(old.summary && cfg.value("enable_topology_refinement",false)) {
        const auto fraction=cfg.value("topology_time_fraction",0.25);
        if(!(fraction>0 && fraction<=1)) throw std::invalid_argument("Invalid topology_time_fraction");
        Deadline local(one,netSeconds*fraction);
        auto refined=refineTopology(graph,id,old.route,local,topologyStats,&view,observe);
        candidates.insert(candidates.end(),std::make_move_iterator(refined.begin()),std::make_move_iterator(refined.end()));
    }
    try {
        if(graph.terminals(id).size()<=128) {
            PatternSearch pattern(graph,id,one);
            Routes routes;
            const auto& net=graph.model.nets[id];
            if(net.localNet[0]>=0 && net.localNet[1]>=0) routes=pattern.enumerateFast(false,old.pattern,&view,observe);
            if(routes.empty()) routes=pattern.enumerateFast(true,old.pattern,&view,observe);
            for(auto& route:routes) candidates.push_back({"pattern",std::move(route)});
        }
        ++result.completed;
    } catch(const SearchTimeout&) {
        ++result.timedOut;
        ++old.failures;
    }
    Route winningRoute{{0,0,0},{}};
    PreparedRoute winner;
    std::string winningOperation;
    const auto previousSummary=old.summary;
    std::map<BlockedResource,bool> completeBlocks;
    result.candidates+=candidates.size();
    auto consider=[&](const Route& route,const PreparedRoute& prepared,const std::string& operation,bool fresh) {
        auto stat=topologyStats.find(operation);
        if(old.summary && equivalentRoutes(old.route,route)) return false;
        const bool feasible=graph.available(*prepared,&view);
        bool related=false;
        if(recordBlocks && !feasible) {
            const auto actual=graph.dynamicBlockers(*prepared,view);
            collect(actual);
            for(const auto& blocked:actual) if(blockers.count(blocked)) completeBlocks[blocked]=true;
            related=!actual.empty();
        }
        if(fresh && feasible && stat!=topologyStats.end()) ++stat->second.submittable;
        if(feasible && incumbent.improves(old.summary,prepared,winner?winner:old.summary)) {
            winningRoute=route;
            winner=prepared;
            winningOperation=operation;
        }
        return related;
    };
    if(keepArchive) for(const auto& cached:archive.entries(id)) {
        if(deadline.expired()) break;
        ++result.candidates;
        consider(*cached.route,cached.summary,cached.operation,false);
    }
    for(auto& candidate:candidates) {
        if(deadline.expired()) break;
        if(old.summary && equivalentRoutes(old.route,candidate.route)) continue;
        PreparedRoute prepared;
        try {prepared=graph.prepareRoute(id,candidate.route);}
        catch(const std::invalid_argument&) {++invalidAlternatives;continue;}
        if(!graph.staticallyAvailable(*prepared)) continue;
        auto stat=topologyStats.find(candidate.operation);
        if(stat!=topologyStats.end()) ++stat->second.staticLegal;
        const bool related=consider(candidate.route,prepared,candidate.operation,true);
        if(archive.insert(id,std::move(candidate.route),prepared,candidate.operation,2,related) && stat!=topologyStats.end())
            ++stat->second.retained;
    }
    if(winner && incumbent.replace(id,old.version,std::move(winningRoute),winner)) {
        ++result.accepted;
        if(topologyStats.count(winningOperation)) {
            auto& stat=topologyStats[winningOperation];
            ++stat.accepted;
            stat.deltaCost+=winner->cost()-previousSummary->cost();
            stat.deltaWire+=winner->wirelength()-previousSummary->wirelength();
            stat.deltaVia+=double(winner->vias())-previousSummary->vias();
            stat.deltaHbt+=double(winner->hbtSites().size())-previousSummary->hbtSites().size();
        }
        old.failures=0;
    }
    if(membership[id]<existing.size() && !archive.entries(id).empty())
        schedule(existing[membership[id]],true,false);
    for(const auto& block:blockers) {
        observedResources.insert(block.occupied);
        for(int owner:graph.users(block.occupied)) if(owner!=id && incumbent.current[owner].summary) {
            const BlockingEvidence evidence{id,owner,block,completeBlocks.count(block)?view.version():observedEpoch,
                                            completeBlocks.count(block)>0};
            schedule({id,owner},false,false,&evidence);
        }
    }
    refreshBudget(id);
}

bool ExchangeStage::appendCase(const std::vector<int>& ids,const std::vector<Route>& proposed,
                                const std::vector<PreparedRoute>& replacement,const ExchangeTask& task,
                                const LocalResult& selected) {
    Json record={{"nets",ids},{"search_complete",selected.complete},{"blocks",Json::array()},
                 {"before",Json::array()},{"after",Json::array()},{"budget_pair",task.budget}};
    bool strictPair=false;
    for(const auto& b:task.blocks) record["blocks"].push_back({{"requester",b.requester},{"owner",b.owner},
        {"requested",b.resource.requested},{"occupied",b.resource.occupied},{"version",b.observedVersion},
        {"complete_candidate",b.completeCandidate}});
    if(ids.size()==2) {
        const auto outside=graph.groupView(ids.front(),ids);
        std::vector<PreparedRoute> original{incumbent.current[ids[0]].summary,incumbent.current[ids[1]].summary};
        auto evaluate=[&](size_t first) {
            auto single=original;
            single[first]=replacement[first];
            const bool feasible=graph.canReplaceGroup(ids,single,outside);
            const auto a=original[first],b=replacement[first];
            const bool improves=incumbent.improves(a,b,a);
            const auto h=incumbent.after(a,b);
            const auto undo=original[1-first],add=replacement[1-first];
            const auto h2=h-undo->hbtSites().size()+add->hbtSites().size();
            const bool second=incumbent.excess(h2)<incumbent.excess(h) ||
               (incumbent.excess(h2)==incumbent.excess(h) && add->cost()+1e-9<undo->cost());
            return Json{{"unilateral_physical",feasible},{"unilateral_accepted",feasible && improves},
                        {"sequence_accepted",feasible && improves && second}};
        };
        record["first_A"]=evaluate(0);
        record["first_B"]=evaluate(1);
        strictPair=selected.selection[0] && selected.selection[1] &&
            !record["first_A"]["sequence_accepted"].get<bool>() && !record["first_B"]["sequence_accepted"].get<bool>();
    }
    if(cases.size()>=8 || !cfg.value("record_topology_exchange_cases",true)) return strictPair;
    Demand removed;
    std::set<Resource> relevant;
    size_t outsideHbt=incumbent.hbtTotal;
    auto capture=[&](const PreparedRoute& prepared) {
        for(const auto& item:prepared->resources()) relevant.insert(item.first);
        for(int site:prepared->hbtSites()) for(int adjacent:graph.spacingNeighbors(site))
            relevant.insert(graph.hbtResource(adjacent));
    };
    for(size_t i=0;i<ids.size();++i) {
        const auto& old=incumbent.current[ids[i]];
        for(const auto& item:old.summary->resources()) removed[item.first]+=item.second;
        outsideHbt-=old.summary->hbtSites().size();
        capture(old.summary);
        capture(replacement[i]);
        record["before"].push_back(routeRecord(old.route,old.summary));
        record["after"].push_back(routeRecord(selected.selection[i]?proposed[i]:old.route,replacement[i]));
    }
    record["ledger_version"]=graph.resourceVersion();
    record["outside_hbt"]=outsideHbt;
    record["hbt_budget"]=incumbent.mode=="budgeted"?Json(incumbent.budget):Json(nullptr);
    record["outside_resources"]=Json::array();
    for(Resource id:relevant) {
        const auto existing=graph.demand().find(id);
        const auto loc=graph.location(id);
        record["outside_resources"].push_back({{"id",id},{"outside",(existing==graph.demand().end()?0:existing->second)-removed[id]},
            {"capacity",graph.capacity(id)},{"type",static_cast<int>(loc.type)},{"site",loc.type==GREdgeType::HBT?loc.x:-1}});
    }
    record["strict_pair"]=strictPair;
    cases.push_back(std::move(record));
    return strictPair;
}

void ExchangeStage::solveNext() {
    const auto start=Clock::now();
    auto node=pending.begin();
    const auto ids=node->first;
    auto task=std::move(node->second);
    pending.erase(node);
    task.blocks.erase(std::remove_if(task.blocks.begin(),task.blocks.end(),[&](const auto& block) {
        const auto& owners=graph.users(block.resource.occupied);
        return !std::binary_search(owners.begin(),owners.end(),block.owner);
    }),task.blocks.end());
    if(!task.inherited && !task.budget && task.blocks.empty()) {++stale;return;}
    if(std::any_of(ids.begin(),ids.end(),[&](int n){return !incumbent.current[n].summary;})) return;
    TaskVersion stamp{graph.resourceVersion(),{}};
    for(int id:ids) {
        import(id);
        stamp.candidates.push_back({incumbent.current[id].version,archive.revision(id)});
    }
    const auto known=completedVersions.find(ids);
    if(known!=completedVersions.end() && known->second==stamp) {++unchanged;return;}
    ++attempts;
    fromBlocking+=!task.blocks.empty();
    fromBudget+=task.budget;
    Deadline one(deadline,netSeconds);
    if(regenerate && !task.blocks.empty() && regenerationGroups<size_t(regenerationLimit)) {
        ++regenerationGroups;
        Measured regen(Timing::ExchangeRegeneration);
        for(int id:ids) {
            if(one.expired()) break;
            auto view=graph.groupView(id,ids);
            Deadline slice(one,netSeconds/ids.size());
            try {
                if(cfg.value("enable_topology_refinement",false)) {
                    Deadline topo(slice,netSeconds/(2*ids.size()));
                    for(auto& candidate:refineTopology(graph,id,incumbent.current[id].route,topo,topologyStats,&view)) {
                        auto summary=graph.prepareRoute(id,candidate.route);
                        if(archive.insert(id,std::move(candidate.route),summary,candidate.operation,2,true)) {
                            ++regenerated;
                            ++topologyStats[candidate.operation].retained;
                            ++topologyStats[candidate.operation].staticLegal;
                        }
                    }
                }
                auto routes=SparseMaze(graph,id,slice,&view).candidates(stride,true);
                for(auto& route:routes) {
                    if(slice.expired()) break;
                    auto summary=graph.prepareRoute(id,route);
                    regenerated+=archive.insert(id,std::move(route),std::move(summary),"group_maze",2,true);
                }
            } catch(const SearchTimeout&) {}
        }
    }
    std::vector<std::vector<PreparedRoute>> pools;
    std::vector<std::vector<const ArchivedCandidate*>> sources;
    std::vector<unsigned> versions;
    size_t outside=incumbent.hbtTotal;
    for(int id:ids) {
        const auto& old=incumbent.current[id];
        versions.push_back(old.version);
        outside-=old.summary->hbtSites().size();
        pools.push_back({old.summary});
        sources.push_back({nullptr});
        for(const auto& candidate:archive.entries(id)) if(!equivalentRoutes(old.route,*candidate.route)) {
            pools.back().push_back(candidate.summary);
            sources.back().push_back(&candidate);
        }
    }
    const auto selected=selectGroup(graph,ids,pools,one,outside,incumbent.budget);
    combinations+=selected.combinations;
    nodes+=selected.visited;
    pruned+=selected.pruned;
    completed+=selected.complete;
    timeouts+=!selected.complete;
    const bool change=std::any_of(selected.selection.begin(),selected.selection.end(),[](size_t n){return n!=0;});
    if(change) {
        std::vector<Route> routes;
        std::vector<PreparedRoute> summaries;
        std::vector<std::string> operations;
        std::vector<PreparedRoute> oldSummaries;
        for(size_t i=0;i<ids.size();++i) {
            const auto k=selected.selection[i];
            const auto& old=incumbent.current[ids[i]];
            routes.push_back(k?*sources[i][k]->route:old.route);
            operations.push_back(k?sources[i][k]->operation:"");
            summaries.push_back(pools[i][k]);
            oldSummaries.push_back(old.summary);
        }
        {
            Measured commit(Timing::ExchangeCommit);
            const auto stored=cases.size();
            const bool strictPair=appendCase(ids,routes,summaries,task,selected);
            if(incumbent.replaceJoint(ids,versions,routes,summaries)) {
                ++accepted;
                strictPairs+=strictPair;
                simultaneous+=std::count_if(selected.selection.begin(),selected.selection.end(),[](size_t n){return n!=0;})>=2;
                for(size_t i=0;i<ids.size();++i) {
                    const auto& before=oldSummaries[i];
                    const auto& after=summaries[i];
                    deltaWire+=after->wirelength()-before->wirelength();
                    deltaVia+=double(after->vias())-before->vias();
                    deltaHbt+=double(after->hbtSites().size())-before->hbtSites().size();
                    deltaCost+=after->cost()-before->cost();
                    if(topologyStats.count(operations[i])) ++topologyStats[operations[i]].jointAccepted;
                }
            } else if(cases.size()>stored) cases.erase(cases.end()-1);
        }
    } else if(selected.complete) {
        stamp.candidates.clear();
        for(int id:ids) stamp.candidates.push_back({incumbent.current[id].version,archive.revision(id)});
        completedVersions[ids]=std::move(stamp);
    }
    for(int id:ids) {
        if(!keepArchive) archive.erase(id);
        if(change || regenerate) refreshBudget(id);
    }
    exchangeSeconds+=std::chrono::duration<double>(Clock::now()-start).count();
}

CoordinatedStage ExchangeStage::run(const std::vector<std::pair<double,int>>& priority,const std::string& output) {
    for(const auto& item:priority) {
        if(deadline.expired()) break;
        const int id=item.second;
        generate(id);
        if(!pending.empty() && !deadline.expired()) solveNext();
        if(!keepArchive) archive.erase(id);
    }
    while(!pending.empty() && !deadline.expired()) solveNext();
    // 专项证据与统计也位于父阶段计时范围内。
    if(!cases.empty() && cfg.value("record_topology_exchange_cases",true)) {
        try {
            std::ofstream stream(output+"/topology_exchanges.json");
            stream.exceptions(std::ios::failbit|std::ios::badbit);
            stream<<cases.dump()<<'\n';
            stream.close();
        } catch(const std::ios_base::failure&) {++evidenceErrors;}
    }
    result.statistics=archive.report();
    const Json stage={{"blocked_resources",observedResources.size()},{"blocker_records_dropped",blockerDropped},
        {"exchange_groups",groups},{"exchange_attempted",attempts},{"exchange_completed",completed},{"exchange_accepted",accepted},
        {"exchange_timeout",timeouts},{"exchange_group_regeneration",regenerationGroups},{"exchange_regenerated",regenerated},
        {"exchange_combinations",combinations},{"exchange_search_nodes",nodes},{"exchange_safe_pruned_nodes",pruned},
        {"exchange_delta_wirelength",deltaWire},{"exchange_delta_via",deltaVia},{"exchange_delta_hbt",deltaHbt},
        {"exchange_delta_objective",deltaCost},{"exchange_runtime_seconds",exchangeSeconds},
        {"exchange_from_blocking",fromBlocking},{"exchange_from_budget",fromBudget},{"exchange_stale",stale},
        {"exchange_multinet_accepted",simultaneous},{"exchange_strict_pair_accepted",strictPairs},
        {"exchange_evidence_io_failed",evidenceErrors},{"archive_invalid_alternatives",invalidAlternatives},
        {"exchange_identical_completed",unchanged},{"exchange_tasks_dropped",taskDropped},{"exchange_tasks_pending",pending.size()},
        {"stop_reason",deadline.expired()?"stage_deadline":"tasks_exhausted"},{"stage_overrun_seconds",deadline.overrun()}};
    for(auto it=stage.begin();it!=stage.end();++it) result.statistics[it.key()]=it.value();
    return std::move(result);
}
}

CoordinatedStage runTopologyExchange(IncumbentSolution& solution,const std::vector<std::pair<double,int>>& priority,
    const std::vector<std::vector<int>>& previous,const Deadline& deadline,double netSeconds,int stride,const std::string& output,
    std::map<std::string,TopologyStats>& topology) {
    return ExchangeStage(solution,deadline,netSeconds,stride,previous,topology).run(priority,output);
}
}
