#include "IncrementalRoute.h"
#include "ConflictOptimizer.h"
#include "Profile.h"
#include "SeedTopology.h"
#include "SparseMaze.h"
#include <future>
#include <numeric>

namespace native3d {
namespace {
using Clock=std::chrono::steady_clock;
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
    explicit IncumbentSolution(RoutingGraph& graph):current(graph.model.nets.size()),graph(graph) {}
    void publish(int net,Route&& route,PreparedRoute summary) {
        graph.commitPrepared(net,summary);
        auto& entry=current[net];
        entry.route=std::move(route);
        entry.summary=std::move(summary);
        ++entry.version;
    }
    Json metrics() const {
        double objective=0,length=0;
        size_t vias=0,hbt=0,connected=0;
        for(const auto& n:current) if(n.summary) {
            objective+=n.summary->cost();length+=n.summary->wirelength();
            vias+=n.summary->vias();hbt+=n.summary->hbtSites().size();++connected;
        }
        return {{"best_feasible",connected==current.size()?Json(objective):Json(nullptr)},
                {"routed_nets",connected},{"wirelength_um",length},{"via_count",vias},{"used_hbt",hbt},
                {"resource_conflicts",0}};
    }
};
bool crosses(const GlobalNet& net) {return net.localNet[0]>=0 && net.localNet[1]>=0;}
bool protectsSites(const RoutingGraph& graph,const PreparedRoute& old,const PreparedRoute& candidate) {
    if(!old || !graph.model.config.value("prefer_hbt_nonincrease",false)) return true;
    return candidate->hbtSites().size()<=old->hbtSites().size();
}
double wirePressure(const RoutingGraph& graph,const RouteResourceSummary& summary) {
    double peak=0;
    for(const auto& r:summary.resources()) {
        if(graph.location(r.first).type==GREdgeType::HBT) break;
        auto used=graph.demand().find(r.first);
        if(used!=graph.demand().end())
            peak=std::max(peak,used->second/std::max(1e-12,graph.capacity(r.first)));
    }
    return peak;
}
std::vector<int> initialOrder(const RoutingGraph& g,const std::string& mode) {
    std::vector<int> order(g.model.nets.size());
    std::iota(order.begin(),order.end(),0);
    if(mode=="local_first" || mode=="cross_first") {
        const bool crossFirst=mode=="cross_first";
        std::stable_partition(order.begin(),order.end(),[&](int id){return crosses(g.model.nets[id])==crossFirst;});
    } else if(mode=="hpwl" || mode=="hbt_scarcity") {
        std::vector<double> rank(order.size());
        for(int id:order) {
            XY lo{{INT64_MAX,INT64_MAX}},hi{{INT64_MIN,INT64_MIN}};
            for(const auto& pin:g.terminals(id)) {
                const auto p=g.center(pin.front());
                for(int a:{0,1}) {lo[a]=std::min(lo[a],p[a]);hi[a]=std::max(hi[a],p[a]);}
            }
            const auto span=double(hi[0]-lo[0])+double(hi[1]-lo[1]);
            rank[id]=mode=="hpwl"?-span:(crosses(g.model.nets[id])?span:std::numeric_limits<double>::infinity());
        }
        std::stable_sort(order.begin(),order.end(),[&](int a,int b){return rank[a]<rank[b];});
    } else if(mode!="input") throw std::invalid_argument("Unknown seed order: "+mode);
    return order;
}
Json optimizeConflicts(IncumbentSolution& solution) {
    auto& graph=solution.graph;
    const auto& cfg=graph.model.config;
    const int maxGroup=cfg.value("local_exact_threshold",8);
    if(maxGroup<2 || maxGroup>32) throw std::invalid_argument("Invalid local exact threshold");
    const double step=cfg.value("pricing_step",0.5);
    if(!std::isfinite(step) || step<0) throw std::invalid_argument("Invalid resource price step");
    const auto seconds=cfg.value("master_seconds",cfg.value("stage_seconds",15.));
    Deadline budget(seconds);
    std::vector<std::vector<PreparedRoute>> choices(solution.current.size());
    for(size_t id=0;id<solution.current.size();++id) {
        const auto& old=solution.current[id];
        if(!old.summary) continue;
        choices[id].push_back(old.summary);
        for(const auto& route:old.alternatives) choices[id].push_back(graph.prepareRoute(id,route));
    }
    const auto conflict=buildConflictGroups(graph,choices,maxGroup,step);
    if(cfg.value("enable_dynamic_pricing",true)) graph.searchPrices(conflict.prices);
    size_t changed=0,attempted=0,completed=0,visited=0;
    size_t regenerated=0,maxWorkers=1;
    const int threads=graph.parameters().threads;
    if(threads<1 || threads>128) throw std::invalid_argument("Invalid native worker count");
    const bool regenerate=cfg.value("enable_group_regeneration",false);
    const auto regenerationLimit=cfg.value("group_regeneration_limit",32);
    if(regenerationLimit<0) throw std::invalid_argument("Invalid regeneration budget");
    try {
        for(const auto& ids:conflict.groups) {
            budget.check();
            ++attempted;
            for(int id:ids) graph.release(id);
            size_t applied=0;
            LocalResult selected;
            try {
                if(regenerate && attempted<=static_cast<size_t>(regenerationLimit)) {
                    Measured generation(Timing::Candidates);
                    // 分量已撤销，所有工作线程共享同一只读外部占用。只在全部返回后发布。
                    std::vector<Routes> produced(ids.size());
                    auto calculate=[&](size_t i) {
                        const int id=ids[i];
                        if(graph.terminals(id).size()>16) return;
                        Deadline search(cfg.value("net_seconds",0.03));
                        try {
                            FastPatternCache cache;
                            produced[i]=PatternSearch(graph,id,search).enumerateFast(false,cache);
                        } catch(const SearchTimeout&) {}
                    };
                    const auto size=std::min<size_t>(threads,ids.size());
                    maxWorkers=std::max(maxWorkers,size);
                    if(size==1) {
                        for(size_t i=0;i<ids.size();++i) calculate(i);
                    } else {
                        std::vector<std::future<void>> workers;
                        for(size_t worker=0;worker<size;++worker)
                            workers.push_back(std::async(std::launch::async,[&,worker]() {
                                for(size_t i=worker;i<ids.size();i+=size) calculate(i);
                            }));
                        for(auto& worker:workers) worker.get();
                    }
                    for(size_t i=0;i<ids.size();++i) {
                        const int id=ids[i];
                        auto& alternatives=solution.current[id].alternatives;
                        auto& candidates=produced[i];
                        std::vector<std::pair<PreparedRoute,size_t>> rank;
                        for(size_t k=0;k<candidates.size();++k)
                            rank.emplace_back(graph.prepareRoute(id,candidates[k]),k);
                        std::stable_sort(rank.begin(),rank.end(),
                            [](const auto& a,const auto& b){return a.first->cost()<b.first->cost();});
                        for(size_t k=0;k<std::min<size_t>(4,rank.size());++k) {
                            choices[id].push_back(rank[k].first);
                            alternatives.push_back(std::move(candidates[rank[k].second]));
                            ++regenerated;
                        }
                    }
                }
                Deadline subproblem(std::min(seconds,0.1));
                selected=selectLocal(graph,ids,choices,subproblem,cfg.value("prefer_hbt_nonincrease",false));
                for(;applied<ids.size();++applied)
                    graph.commitPrepared(ids[applied],choices[ids[applied]][selected.selection[applied]]);
            } catch(...) {
                while(applied) graph.release(ids[--applied]);
                for(int id:ids) graph.commitPrepared(id,solution.current[id].summary);
                throw;
            }
            for(size_t i=0;i<ids.size();++i) if(auto index=selected.selection[i]) {
                auto& old=solution.current[ids[i]];
                std::swap(old.route,old.alternatives[index-1]);
                old.summary=choices[ids[i]][index];
                ++changed;
                ++old.version;
            }
            completed+=selected.complete;
            visited+=selected.visited;
        }
    } catch(const SearchTimeout&) {
        // 已接受的分量保留，未开始的分量继续使用原有可行树。
    }
    return {{"groups",conflict.groups.size()},{"groups_attempted",attempted},{"groups_completed",completed},
            {"contested_resources",conflict.contested},{"priced_resources",conflict.prices.size()},
            {"nets_changed",changed},{"visited",visited},{"regenerated_candidates",regenerated},{"workers",maxWorkers}};
}
}

IncrementalResult routeIncrementally(RoutingGraph& graph,const std::vector<int>& stages,Json& report) {
    const auto& cfg=graph.model.config;
    IncumbentSolution incumbent(graph);
    const int stride=cfg.value("sparse_stride",4);
    const double seedSeconds=cfg.value("seed_seconds",1200.);
    const double stageSeconds=cfg.value("stage_seconds",15.);
    const double netSeconds=cfg.value("net_seconds",0.03);
    const auto selectedOrder=cfg.value("seed_order",std::string("local_first"));
    const auto order=initialOrder(graph,selectedOrder);
    report["algorithm"]={{"routing_mode","fast"},{"seed_order",selectedOrder},{"net_seconds",netSeconds}};
    report["bound_scope"]="local_candidate_updates";
    report["unrestricted_optimality_gap"]=nullptr;
    for(int stage:stages) {
        Measured stageTime(stage==1?Timing::Stage1:stage==2?Timing::Stage2:Timing::Stage3);
        const auto start=Clock::now();
        size_t attempted=0,completed=0,accepted=0,restored=0,timedOut=0,candidateCount=0;
        bool stageLimit=false;
        Json conflict=nullptr;
        if(stage==1) {
            Measured seed(Timing::Seed);
            Deadline budget(seedSeconds);
            try {
                for(int id:order) {
                    budget.check();
                    ++attempted;
                    MeasuredNet measureNet(id,NetMethod::Seed);
                    const auto begin=Clock::now();
                    auto candidates=initialCandidates(graph,id,budget);
                    if(candidates.empty()) {
                        Profile::retry();
                        MeasuredNet maze(id,NetMethod::Maze);
                        candidates=SparseMaze(graph,id,budget).seed(stride,true);
                    }
                    ++completed;
                    candidateCount+=candidates.size();
                    if(candidates.empty()) {
                        ++incumbent.current[id].failures;
                    } else {
                        auto summary=graph.prepareRoute(id,candidates.front());
                        incumbent.publish(id,std::move(candidates.front()),std::move(summary));
                        if(candidates.size()>1) {
                            auto& stored=incumbent.current[id].alternatives;
                            stored.assign(std::make_move_iterator(candidates.begin()+1),std::make_move_iterator(candidates.end()));
                        }
                        ++accepted;
                    }
                    const auto elapsed=std::chrono::duration<double>(Clock::now()-begin).count();
                    if(elapsed>=1 || candidates.empty())
                        std::cout<<"NATIVE_SEED_NET "<<id<<" pins="<<graph.terminals(id).size()
                                 <<" seconds="<<elapsed<<" candidates="<<candidates.size()<<std::endl;
                }
            } catch(const SearchTimeout&) {stageLimit=true;}
            report["seed_attempted"]=attempted;
            report["seed_feasible_nets"]=accepted;
        } else {
            if(stage==2) {
                if(cfg.value("enable_conflict_optimizer",true)) conflict=optimizeConflicts(incumbent);
                graph.captureCongestion();
            }
            std::vector<std::pair<double,int>> priority;
            for(int id:order) {
                const auto& current=incumbent.current[id];
                const auto pins=graph.terminals(id).size();
                if(!current.summary) {
                    priority.emplace_back(-1000000.-current.failures,id);
                    continue;
                }
                const bool cross=crosses(graph.model.nets[id]);
                const double pressure=wirePressure(graph,*current.summary);
                const bool hasAlternative=!current.alternatives.empty();
                if(stage==2 && (cross || pressure>=0.85 || hasAlternative)) {
                    const double fastNet=pins<=8?8.:0.;
                    priority.emplace_back(-(10*pressure+std::min<size_t>(8,current.summary->hbtSites().size())+hasAlternative+fastNet),id);
                }
                if(stage==3 && (current.failures || (pressure>=0.95 && pins>2 && pins<=64))) {
                    priority.emplace_back(-(10*pressure+current.failures),id);
                }
            }
            std::sort(priority.begin(),priority.end());
            Deadline budget(stageSeconds);
            try {
                for(const auto& item:priority) {
                    budget.check();
                    const int id=item.second;
                    auto& old=incumbent.current[id];
                    ++attempted;
                    MeasuredNet measureNet(id,stage==2?NetMethod::Detour:NetMethod::Maze);
                    // 保留旧摘要及连通树。新候选只观察除该网外的实际占用。
                    if(old.summary) graph.release(id);
                    bool applied=false;
                    try {
                        Measured generation(Timing::Candidates);
                        Routes candidates;
                        if(stage==2) candidates.swap(old.alternatives);
                        Deadline one(netSeconds);
                        try {
                            Routes newRoutes;
                            if(stage==2) {
                                // 高扇出网沿用带分簇的初解和分支修复。整网投影可能超过 FLUTE 的整数范围。
                                if(graph.terminals(id).size()<=128) {
                                    PatternSearch pattern(graph,id,one);
                                    if(crosses(graph.model.nets[id])) newRoutes=pattern.enumerateFast(false,old.pattern);
                                    if(newRoutes.empty()) newRoutes=pattern.enumerateFast(true,old.pattern);
                                }
                            } else newRoutes=SparseMaze(graph,id,one).candidates(stride,true);
                            candidates.insert(candidates.end(),std::make_move_iterator(newRoutes.begin()),
                                              std::make_move_iterator(newRoutes.end()));
                            ++completed;
                        } catch(const SearchTimeout&) {
                            ++timedOut;
                            ++old.failures;
                        }
                        candidateCount+=candidates.size();
                        size_t best=candidates.size();
                        PreparedRoute winner;
                        double cost=old.summary?old.summary->cost():std::numeric_limits<double>::infinity();
                        for(size_t i=0;i<candidates.size();++i) {
                            auto summary=graph.prepareRoute(id,candidates[i]);
                            if(protectsSites(graph,old.summary,summary) &&
                               summary->cost()+1e-9<cost && graph.available(*summary)) {
                                cost=summary->cost();winner=std::move(summary);best=i;
                            }
                        }
                        if(winner) {
                            incumbent.publish(id,std::move(candidates[best]),std::move(winner));
                            ++accepted;
                            old.failures=0;
                            applied=true;
                        }
                    } catch(...) {
                        if(!applied && old.summary) graph.commitPrepared(id,old.summary);
                        throw;
                    }
                    if(!applied && old.summary) {
                        graph.commitPrepared(id,old.summary);
                        ++restored;
                    }
                }
            } catch(const SearchTimeout&) {stageLimit=true;}
        }
        auto result=incumbent.metrics();
        result["stage"]=stage;
        result["solver_status"]=result["best_feasible"].is_null()?"HEURISTIC_PARTIAL":"HEURISTIC_FEASIBLE_UPDATES";
        result["candidate_generation"]=stageLimit?"STAGE_LIMIT":"SELECTIVE";
        result["candidate_nets_completed"]=completed;
        result["nets_attempted"]=attempted;
        result["candidate_net_timeouts"]=timedOut;
        result["accepted_nets"]=accepted;
        result["restored_nets"]=restored;
        result["pool_size"]=candidateCount;
        result["lower_bound"]=nullptr;
        result["gap"]=nullptr;
        result["seconds"]=std::chrono::duration<double>(Clock::now()-start).count();
        result["local_optimizer"]=std::move(conflict);
        report["stages"].push_back(result);
        std::cout<<"NATIVE_STAGE "<<result.dump()<<std::endl;
    }
    IncrementalResult result;
    for(size_t id=0;id<incumbent.current.size();++id)
        if(!incumbent.current[id].summary) result.unrouted.push_back(id);
    if(result.unrouted.empty()) {
        result.routes.reserve(incumbent.current.size());
        for(auto& entry:incumbent.current) result.routes.push_back(std::move(entry.route));
    }
    return result;
}
}
