#include "IncrementalRoute.h"
#include "ConflictOptimizer.h"
#include "Profile.h"
#include "ParallelMaze.h"
#include "MazeExperiment.h"
#include "SeedTopology.h"
#include "SparseMaze.h"
#include "TopologyRefinement.h"
#include "TopologyExchange.h"
#include "FailureRepair.h"
#include <future>
#include <numeric>
#include <sys/resource.h>

namespace native3d {
IncumbentSolution::IncumbentSolution(RoutingGraph& g):current(g.model.nets.size()),graph(g),
    mode(g.model.config.value("optimization_mode",std::string("baseline"))),
    protect(g.model.config.value("prefer_hbt_nonincrease",false)) {
    const auto& cfg=g.model.config;
    if(mode!="baseline" && mode!="joint" && mode!="budgeted") throw std::invalid_argument("Unknown optimization_mode");
    if(mode!="baseline" && protect) throw std::invalid_argument("Joint or budgeted mode requires prefer_hbt_nonincrease=false");
    if(mode=="budgeted") {
        int limit=integer(cfg.at("hbt_budget"));
        if(limit<0) throw std::invalid_argument("Negative HBT budget");
        budget=limit;
    } else if(cfg.count("hbt_budget")) throw std::invalid_argument("hbt_budget requires budgeted mode");
}
size_t IncumbentSolution::after(const PreparedRoute& old,const PreparedRoute& candidate) const {
    const size_t previous=old?old->hbtSites().size():0;
    if(previous>hbtTotal) throw std::logic_error("Inconsistent incumbent HBT total");
    return hbtTotal-previous+(candidate?candidate->hbtSites().size():0);
}
bool IncumbentSolution::improves(const PreparedRoute& old,const PreparedRoute& candidate,const PreparedRoute& best) const {
    if(old && protect && candidate->hbtSites().size()>old->hbtSites().size()) return false;
    if(!best) return true; // 接通真实终端优先，超预算的完整初解进入显式修复。
    auto a=excess(after(old,candidate)),b=excess(after(old,best));
    return a<b || (a==b && candidate->cost()+1e-9<best->cost());
}
void IncumbentSolution::publish(int net,Route&& route,PreparedRoute summary) {
    auto& entry=current.at(net);
    const auto next=after(entry.summary,summary);
    graph.commitPrepared(net,summary);
    hbtTotal=next;
    entry.route.edges.swap(route.edges);
    entry.route.root=route.root;
    entry.summary=std::move(summary);
    ++entry.version;
}
bool IncumbentSolution::replace(int net,unsigned version,Route&& route,PreparedRoute summary) {
    auto& entry=current.at(net);
    if(entry.version!=version) return false;
    if(!entry.summary) {
        if(!graph.available(*summary)) return false;
        publish(net,std::move(route),std::move(summary));
        return true;
    }
    const auto next=after(entry.summary,summary);
    if(!graph.replacePrepared(net,summary)) return false;
    hbtTotal=next;
    // 账本已发布，此后的交换、摘要移动及版本更新均不分配内存。
    entry.route.edges.swap(route.edges);
    entry.route.root=route.root;
    entry.summary=std::move(summary);
    ++entry.version;
    return true;
}
bool IncumbentSolution::replaceJoint(const std::vector<int>& ids,const std::vector<unsigned>& versions,
                                    std::vector<Route>& routes,const std::vector<PreparedRoute>& summaries) {
    if(ids.size()<2 || ids.size()!=versions.size() || ids.size()!=routes.size() || ids.size()!=summaries.size())
        throw std::invalid_argument("Invalid incumbent transaction");
    if(!std::is_sorted(ids.begin(),ids.end()) || std::adjacent_find(ids.begin(),ids.end())!=ids.end())
        throw std::invalid_argument("Joint transaction requires unique ordered nets");
    if(mode=="baseline") throw std::logic_error("Joint transaction requires joint or budgeted mode");
    size_t outside=hbtTotal,total=0;
    long double before=0,after=0;
    for(size_t i=0;i<ids.size();++i) {
        const auto& previous=current.at(ids[i]);
        if(previous.version!=versions[i] || !previous.summary || !summaries[i]) return false;
        outside-=previous.summary->hbtSites().size();
        total+=summaries[i]->hbtSites().size();
        before+=previous.summary->cost();
        after+=summaries[i]->cost();
    }
    total+=outside;
    if(excess(total)>excess(hbtTotal) || (excess(total)==excess(hbtTotal) && after+1e-9L>=before)) return false;
    if(!graph.replaceGroup(ids,summaries)) return false;
    // 所有可能分配内存的候选和索引已准备完毕。提交后仅发布无分配的上层状态。
    hbtTotal=total;
    for(size_t i=0;i<ids.size();++i) {
        auto& previous=current[ids[i]];
        if(previous.summary==summaries[i]) continue;
        previous.route.edges.swap(routes[i].edges);
        previous.route.root=routes[i].root;
        previous.summary=summaries[i];
        ++previous.version;
        previous.failures=0;
    }
    return true;
}
Json IncumbentSolution::metrics() const {
    double objective=0,length=0;
    size_t vias=0,hbt=0,connected=0;
    for(const auto& n:current) if(n.summary) {
        objective+=n.summary->cost();length+=n.summary->wirelength();
        vias+=n.summary->vias();hbt+=n.summary->hbtSites().size();++connected;
    }
    if(hbt!=hbtTotal) throw std::logic_error("Mismatch between HBT count and incumbent trees");
    return {{"best_feasible",connected==current.size() && !excess(hbt)?Json(objective):Json(nullptr)},
            {"hbt_budget_excess",excess(hbt)},{"base_objective_connected_routes",objective},
            {"routed_nets",connected},{"wirelength_um",length},{"via_count",vias},{"used_hbt",hbt},
            {"resource_conflicts",0}};
}
namespace {
using Clock=std::chrono::steady_clock;
bool crosses(const GlobalNet& net) {return net.localNet[0]>=0 && net.localNet[1]>=0;}
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
Json optimizeConflicts(IncumbentSolution& solution,const Deadline* parent=nullptr,
                       std::vector<std::vector<int>>* suppliedGroups=nullptr) {
    Measured solveTime(Timing::Conflict);
    auto& graph=solution.graph;
    const auto& cfg=graph.model.config;
    const int maxGroup=cfg.value("local_exact_threshold",8);
    if(maxGroup<2 || maxGroup>32) throw std::invalid_argument("Invalid local exact threshold");
    const double step=cfg.value("pricing_step",0.5);
    if(!std::isfinite(step) || step<0) throw std::invalid_argument("Invalid resource price step");
    const auto seconds=cfg.value("master_seconds",cfg.value("stage_seconds",15.));
    Deadline budget=parent?Deadline(*parent,seconds):Deadline(seconds);
    std::vector<std::vector<PreparedRoute>> choices(solution.current.size());
    for(size_t id=0;id<solution.current.size();++id) {
        if(parent && budget.expired()) return {{"phase_timeout",true},{"workers",1}};
        const auto& old=solution.current[id];
        if(!old.summary) continue;
        choices[id].push_back(old.summary);
        for(const auto& route:old.alternatives) choices[id].push_back(graph.prepareRoute(id,route));
    }
    const auto conflict=buildConflictGroups(graph,choices,maxGroup,step);
    if(suppliedGroups) *suppliedGroups=conflict.groups;
    if(cfg.value("enable_dynamic_pricing",true)) graph.searchPrices(conflict.prices);
    size_t changed=0,attempted=0,completed=0,visited=0;
    size_t regenerated=0,maxWorkers=1;
    const int threads=integer(cfg.value("conflict_workers",Json(graph.parameters().threads)));
    if(threads<1 || threads>graph.parameters().threads || threads>128) throw std::invalid_argument("Invalid native worker count");
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
                        const auto netTime=cfg.value("net_seconds",0.03);
                        Deadline search=parent?Deadline(budget,netTime):Deadline(netTime);
                        try {
                            FastPatternCache cache;
                            produced[i]=PatternSearch(graph,id,search).enumerateFast(false,cache);
                        } catch(const SearchTimeout&) {}
                    };
                    const auto size=std::min<size_t>(threads,ids.size());
                    maxWorkers=std::max(maxWorkers,size);
                    auto* parentProfile=Profile::current();
                    if(size==1) {
                        for(size_t i=0;i<ids.size();++i) calculate(i);
                    } else {
                        std::vector<std::future<void>> workers;
                        for(size_t worker=0;worker<size;++worker)
                            workers.push_back(std::async(std::launch::async,[&,worker]() {
                                for(size_t i=worker;i<ids.size();i+=size)
                                    Profile::worker(parentProfile,ids[i],NetMethod::Pattern,[&]{calculate(i);});
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
                Deadline subproblem=parent?Deadline(budget,std::min(seconds,0.1)):Deadline(std::min(seconds,0.1));
                size_t outside=solution.hbtTotal;
                for(int id:ids) outside-=solution.current[id].summary->hbtSites().size();
                const size_t room=solution.budget>outside?solution.budget-outside:0;
                selected=selectLocal(graph,ids,choices,subproblem,solution.protect,room);
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
                solution.hbtTotal=solution.after(old.summary,choices[ids[i]][index]);
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

IncrementalResult routeIncrementally(RoutingGraph& graph,const std::vector<int>& stages,Json& report,const std::string& outputDirectory) {
    const auto& cfg=graph.model.config;
    IncumbentSolution incumbent(graph);
    const bool fromCheckpoint=cfg.count("load_maze_checkpoint");
    const bool stage2Replay=cfg.count("load_stage2_checkpoint");
    if(stage2Replay) {
        if(fromCheckpoint || stages!=std::vector<int>{2})
            throw std::invalid_argument("Stage 2 replay requires stages=[2]");
        loadStage2Checkpoint(incumbent,cfg.at("load_stage2_checkpoint").get<std::string>());
    }
    std::vector<std::pair<double,int>> frozenOrder;
    if(fromCheckpoint) {
        if(stages!=std::vector<int>{3}) throw std::invalid_argument("Maze replay requires stages=[3]");
        frozenOrder=loadMazeCheckpoint(incumbent,cfg.at("load_maze_checkpoint").get<std::string>());
    }
    const int checks=integer(cfg.value("maze_trial_checks",Json(0))),taskLimit=integer(cfg.value("maze_trial_tasks",Json(0)));
    if(checks<0 || taskLimit<0 || (!fromCheckpoint && (checks || taskLimit))) throw std::invalid_argument("Invalid fixed workload");
    const int stride=cfg.value("sparse_stride",4);
    const double seedSeconds=cfg.value("seed_seconds",1200.);
    const double stageSeconds=cfg.value("stage_seconds",15.);
    const double netSeconds=cfg.value("net_seconds",0.03);
    const bool coordinated=cfg.value("enable_topology_coordination",false);
    if(coordinated && incumbent.mode=="baseline") throw std::invalid_argument("Topology coordination requires joint or budgeted mode");
    for(const auto option:{"enable_topology_archive","enable_topology_exchange","enable_topology_safe_pruning",
                           "enable_topology_group_regeneration","enable_topology_budget_exchange","enable_topology_rotation"})
        if(cfg.value(option,false) && !coordinated) throw std::invalid_argument(std::string(option)+" requires topology coordination");
    const auto selectedOrder=cfg.value("seed_order",std::string("local_first"));
    const auto order=initialOrder(graph,selectedOrder);
    report["algorithm"]={{"routing_mode","fast"},{"seed_order",selectedOrder},{"net_seconds",netSeconds}};
    report["algorithm"]["optimization_mode"]=incumbent.mode;
    report["bound_scope"]="local_candidate_updates";
    report["unrestricted_optimality_gap"]=nullptr;
    for(int stage:stages) {
        if(stage==2 && cfg.value("save_stage2_checkpoint",false)) {
            if(stage2Replay || stages.front()!=1) throw std::invalid_argument("Stage 2 snapshot requires a fresh Stage 1");
            saveStage2Checkpoint(incumbent,outputDirectory+"/stage2.checkpoint");
        }
        Measured stageTime(stage==1?Timing::Stage1:stage==2?Timing::Stage2:Timing::Stage3);
        const auto start=Clock::now();
        size_t attempted=0,completed=0,accepted=0,restored=0,timedOut=0,candidateCount=0;
        bool stageLimit=false;
        Json conflict=nullptr,parallelMaze=nullptr,exchange=nullptr;
        uint64_t serialExpansions=0,serialRefineTimeouts=0;
        double serialSearch=0;
        std::map<std::string,TopologyStats> topologyStats;
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
                        Measured fallback(Timing::SeedMaze);
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
            std::unique_ptr<Deadline> stageParent;
            std::vector<std::vector<int>> legacyGroups;
            if(stage==2) {
                const bool conflictEnabled=cfg.value("enable_conflict_optimizer",true);
                if(coordinated) stageParent.reset(new Deadline(stageSeconds+(conflictEnabled?cfg.value("master_seconds",stageSeconds):0.)));
                if(conflictEnabled) conflict=optimizeConflicts(incumbent,stageParent.get(),coordinated?&legacyGroups:nullptr);
                graph.captureCongestion();
            }
            std::vector<std::pair<double,int>> priority;
            if(fromCheckpoint) priority=frozenOrder;
            else for(int id:order) {
                const auto& current=incumbent.current[id];
                const auto pins=graph.terminals(id).size();
                if(!current.summary) {
                    priority.emplace_back(-1000000.-current.failures,id);
                    continue;
                }
                const bool cross=crosses(graph.model.nets[id]);
                const double pressure=wirePressure(graph,*current.summary);
                const bool hasAlternative=!current.alternatives.empty();
                if(incumbent.excess(incumbent.hbtTotal) && !current.summary->hbtSites().empty()) {
                    priority.emplace_back(-1000.-current.summary->hbtSites().size(),id);
                    continue;
                }
                const bool augmentation=stage==2 && coordinated && cfg.value("enable_topology_refinement",false) &&
                    cfg.value("enable_hbt_augmentation",false) && current.summary->wirelength()>0;
                if(stage==2 && (cross || pressure>=0.85 || hasAlternative || augmentation)) {
                    const double fastNet=pins<=8?8.:0.;
                    priority.emplace_back(-(10*pressure+std::min<size_t>(8,current.summary->hbtSites().size())+hasAlternative+fastNet),id);
                }
                if(stage==3 && (current.failures || (pressure>=0.95 && pins>2 && pins<=64))) {
                    priority.emplace_back(-(10*pressure+current.failures),id);
                }
            }
            std::sort(priority.begin(),priority.end());
            if(stage==3 && cfg.value("save_maze_checkpoint",false))
                saveMazeCheckpoint(incumbent,priority,outputDirectory+"/stage3.checkpoint");
            if(taskLimit && priority.size()>size_t(taskLimit)) priority.resize(taskLimit);
            if(stage==3 && cfg.count("maze_work_study"))
                report["maze_work_study"]=measureMazeSearch(incumbent,priority,stride);
            Deadline budget(stageSeconds);
            if(stage==3 && cfg.value("enable_failure_driven_repair",false)) {
                Deadline repair(budget,stageSeconds*0.25);
                report["failure_repair"]=repairFailedNets(incumbent,priority,repair,netSeconds,stride);
            }
            const bool parallel=stage==3 && cfg.value("enable_parallel_maze",false);
            int workers=graph.parameters().threads;
            bool serialMemory=false;
            if(parallel && cfg.value("enable_maze_memory_budget",false)) {
                const auto reserve=integer(cfg.value("maze_worker_reserve_kb",Json(16384)));
                const double fraction=cfg.value("maze_memory_fraction",0.18);
                if(reserve<=0 || !(fraction>0 && fraction<=1.)) throw std::invalid_argument("Invalid maze worker memory budget");
                rusage usage{};
                if(getrusage(RUSAGE_SELF,&usage)!=0) throw std::runtime_error("Cannot measure route memory");
                workers=std::max(1,std::min(workers,int(fraction*usage.ru_maxrss/reserve)));
                serialMemory=workers==1;
                report["maze_memory_policy"]={{"rss_before_stage3_kb",usage.ru_maxrss},{"worker_reserve_kb",reserve},
                    {"fraction",fraction},{"worker_limit",workers},{"serial_fallback",serialMemory}};
            }
            if(stageParent) {
                auto run=runTopologyExchange(incumbent,priority,legacyGroups,*stageParent,netSeconds,stride,outputDirectory,topologyStats);
                attempted=run.attempted;
                completed=run.completed;
                accepted=run.accepted;
                timedOut=run.timedOut;
                candidateCount=run.candidates;
                exchange=std::move(run.statistics);
                stageLimit=stageParent->expired();
            } else if(parallel && !serialMemory) {
                parallelMaze=routeMazeBatches(incumbent,priority,budget,stride,netSeconds,workers);
                attempted=parallelMaze.at("maze_tasks").get<size_t>();
                accepted=parallelMaze.at("accepted").get<size_t>();
                completed=parallelMaze.at("completed").get<size_t>();
                restored=parallelMaze.at("restored").get<size_t>();
                candidateCount=parallelMaze.at("candidates").get<size_t>();
                timedOut=parallelMaze.at("maze_search_timeouts").get<size_t>();
                stageLimit=budget.expired();
            } else try {
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
                        Deadline one(budget,netSeconds);
                        if(stage==3 && checks) one.limitChecks(checks);
                        const size_t topologyBegin=candidates.size();
                        std::vector<std::string> operators;
                        if(stage==2 && cfg.value("enable_topology_refinement",false) && old.summary &&
                           (old.summary->hbtSites().size() || old.failures || wirePressure(graph,*old.summary)>=0.85)) {
                            const double fraction=cfg.value("topology_time_fraction",0.25);
                            if(!(fraction>0 && fraction<=1)) throw std::invalid_argument("Invalid topology_time_fraction");
                            Deadline local(one,netSeconds*fraction);
                            auto changed=refineTopology(graph,id,old.route,local,topologyStats);
                            for(auto& move:changed) {operators.push_back(move.operation);candidates.push_back(std::move(move.route));}
                        }
                        try {
                            Routes newRoutes;
                            if(stage==2) {
                                // 高扇出网沿用带分簇的初解和分支修复。整网投影可能超过 FLUTE 的整数范围。
                                if(graph.terminals(id).size()<=128) {
                                    PatternSearch pattern(graph,id,one);
                                    if(crosses(graph.model.nets[id])) newRoutes=pattern.enumerateFast(false,old.pattern);
                                    if(newRoutes.empty()) newRoutes=pattern.enumerateFast(true,old.pattern);
                                }
                            } else {
                                Measured maze(Timing::Stage3Maze);
                                MazeEffort effort;
                                const auto begin=Clock::now();
                                try {newRoutes=SparseMaze(graph,id,one,nullptr,&effort).candidates(stride,true);}
                                catch(const SearchTimeout&) {
                                    serialExpansions+=effort.expanded;
                                    serialSearch+=std::chrono::duration<double>(Clock::now()-begin).count();
                                    throw;
                                }
                                serialExpansions+=effort.expanded;serialRefineTimeouts+=effort.refineTimeout;
                                serialSearch+=std::chrono::duration<double>(Clock::now()-begin).count();
                            }
                            candidates.insert(candidates.end(),std::make_move_iterator(newRoutes.begin()),
                                              std::make_move_iterator(newRoutes.end()));
                            ++completed;
                        } catch(const SearchTimeout&) {
                            if(stage==3) Profile::count(Event::MazeTimeouts);
                            ++timedOut;
                            ++old.failures;
                        }
                        candidateCount+=candidates.size();
                        size_t best=candidates.size();
                        PreparedRoute winner;
                        for(size_t i=0;i<candidates.size();++i) {
                            auto summary=graph.prepareRoute(id,candidates[i]);
                            if(incumbent.improves(old.summary,summary,winner?winner:old.summary) && graph.available(*summary)) {
                                winner=std::move(summary);best=i;
                            } else if(i>=topologyBegin && i-topologyBegin<operators.size()) {
                                auto& stat=topologyStats[operators[i-topologyBegin]];
                                if(incumbent.excess(incumbent.after(old.summary,summary))>incumbent.excess(incumbent.hbtTotal))
                                    ++stat.budget;
                                else ++stat.cost;
                            }
                        }
                        if(winner) {
                            if(best>=topologyBegin && best-topologyBegin<operators.size()) {
                                auto& stat=topologyStats[operators[best-topologyBegin]];
                                ++stat.accepted;stat.deltaCost+=winner->cost()-old.summary->cost();
                                stat.deltaWire+=winner->wirelength()-old.summary->wirelength();
                                stat.deltaVia+=double(winner->vias())-old.summary->vias();
                                stat.deltaHbt+=double(winner->hbtSites().size())-old.summary->hbtSites().size();
                            }
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
        if(!parallelMaze.is_null()) result["parallel_maze"]=std::move(parallelMaze);
        else if(stage==3) result["serial_maze"]={{"astar_expanded_nodes",serialExpansions},
            {"maze_refinement_timeouts",serialRefineTimeouts},{"maze_search_wall_seconds",serialSearch}};
        if(!exchange.is_null()) result["topology_exchange"]=std::move(exchange);
        for(const auto& s:topologyStats) result["topology_refinement"][s.first]=s.second.report();
        result["local_optimizer"]=std::move(conflict);
        report["stages"].push_back(result);
        std::cout<<"NATIVE_STAGE "<<result.dump()<<std::endl;
    }
    IncrementalResult result;
    if(incumbent.mode=="budgeted") {
        report["hbt_budget"]=incumbent.budget;
        report["hbt_budget_excess"]=incumbent.excess(incumbent.hbtTotal);
        report["hbt_budget_used"]=incumbent.hbtTotal;
    }
    for(size_t id=0;id<incumbent.current.size();++id)
        if(!incumbent.current[id].summary) result.unrouted.push_back(id);
    if(result.unrouted.empty()) {
        result.routes.reserve(incumbent.current.size());
        for(auto& entry:incumbent.current) result.routes.push_back(std::move(entry.route));
    }
    return result;
}
}
