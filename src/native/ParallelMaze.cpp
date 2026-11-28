#include "ParallelMaze.h"
#include "Profile.h"
#include "SparseMaze.h"
#include "WorkerPool.h"
#include "MedianRegion.h"
#include <time.h>
#include <deque>
#include <unordered_set>

namespace native3d {
namespace {
using Clock=std::chrono::steady_clock;
double elapsed(Clock::time_point start) {return std::chrono::duration<double>(Clock::now()-start).count();}
double cpuSeconds() {
    timespec ts;
    return clock_gettime(CLOCK_THREAD_CPUTIME_ID,&ts)==0?ts.tv_sec+ts.tv_nsec*1e-9:-1.;
}
struct Alternative {
    Route route;
    PreparedRoute summary;
};
struct MazeTask {
    int id;
    unsigned version;
    PreparedRoute previous;
    ResourceReadView view;
};
struct MazeResult {
    std::vector<Alternative> improvements;
    size_t generated=0,qualityRejected=0;
    size_t worker=0;
    double seconds=0,cpu=0,wait=0;
    uint64_t expansions=0;
    bool started=false,timeout=false,refineTimeout=false;
};

// 仅以稀疏资源键预测争用。即使预测集合完全不交叠，提交时仍检查真实累计需求。
std::vector<Resource> contentionKeys(const RoutingGraph& graph,int net,const PreparedRoute& old) {
    std::vector<Resource> keys;
    auto addSite=[&](const Site& site) {
        keys.push_back(graph.hbtResource(site.id));
        for(int adjacent:graph.spacingNeighbors(site.id)) keys.push_back(graph.hbtResource(adjacent));
        for(const auto& transition:site.transitions) for(auto p:{transition.a,transition.b})
            for(const auto& edge:graph.neighbors(p,net))
                if(edge.type!=GREdgeType::HBT) keys.push_back(graph.resourceId(edge));
    };
    if(old) {
        for(const auto& entry:old->resources()) {
            const auto used=graph.demand().find(entry.first);
            if(used!=graph.demand().end() && used->second>=0.85*graph.capacity(entry.first))
                keys.push_back(entry.first);
        }
        for(int id:old->hbtSites()) addSite(graph.sites.at(id));
    }
    // 端点及 L1 中位矩形给出两侧搜索区域的代理，库索引返回真实站点。
    std::vector<XY> points;
    std::vector<unsigned> weights;
    for(const auto& pin:graph.terminals(net)) {
        const auto p=pin.front();
        points.push_back(graph.center(p));
        weights.push_back(graph.l1Weight(p.dieIdx));
        for(const auto& edge:graph.neighbors(p,net)) keys.push_back(graph.resourceId(edge));
    }
    if(!points.empty()) {
        const auto region=weightedMedianRegion(points,weights);
        for(const auto site:graph.sitesNearRegion(region.low,region.high,4))
            if(site->owner<0 || site->owner==net) addSite(*site);
    }
    std::sort(keys.begin(),keys.end());
    keys.erase(std::unique(keys.begin(),keys.end()),keys.end());
    return keys;
}
}
Json routeMazeBatches(IncumbentSolution& incumbent,const std::vector<std::pair<double,int>>& priority,
                      const Deadline& stage,int stride,double netSeconds,unsigned workerLimit) {
    auto& graph=incumbent.graph;
    const auto& config=graph.model.config;
    const bool protect=incumbent.protect;
    const int requested=graph.parameters().threads;
    const int factor=integer(config.value("maze_batch_factor",Json(2)));
    if(requested<1 || requested>128 || factor<1 || factor>32) throw std::invalid_argument("Invalid maze batch size");
    const int maxRetries=integer(config.value("maze_max_retries",Json(2)));
    if(maxRetries<0 || maxRetries>16) throw std::invalid_argument("Invalid maze retry limit");
    const bool guided=config.value("enable_conflict_aware_batching",false);
    const bool adapt=config.value("enable_adaptive_parallelism",false);
    const int checks=integer(config.value("maze_trial_checks",Json(0)));
    const double low=config.value("maze_conflict_low",0.05),high=config.value("maze_conflict_high",0.20);
    if(!(low>=0 && low<high && high<=1)) throw std::invalid_argument("Invalid maze conflict thresholds");
    const auto count=std::min<size_t>(workerLimit,priority.size());
    auto* profile=Profile::current();
    std::unique_ptr<WorkerPool> pool;
    if(count) pool.reset(new WorkerPool(count));
    size_t attempted=0,completed=0,accepted=0,restored=0,expired=0,searchFailed=0,refineExpired=0;
    size_t generated=0,quality=0,conflicts=0,stale=0,verified=0,batches=0,peakWorkers=0,budgetRejected=0;
    uint64_t expansions=0;
    double searchWall=0,workerElapsed=0,workerCPU=0,queueWait=0,commitSeconds=0,scheduleSeconds=0;
    bool cpuKnown=true;
    std::deque<int> pending;
    for(const auto& item:priority) pending.push_back(item.second);
    std::map<int,int> retries;
    size_t retryCount=0,serialFallback=0,unresolved=0;
    size_t batchTarget=count*factor,deferred=0,shrank=0,grew=0,queuedRetries=0,notStarted=0;
    const auto initialTarget=batchTarget;
    while(!pending.empty() && !stage.expired()) {
        const auto scheduleStart=Clock::now();
        // 重试最后一次单独成批。仍观察最新外部占用，旧解在失败时继续占用资源。
        const bool single=retries.count(pending.front()) && retries.at(pending.front())==maxRetries;
        serialFallback+=single;
        const size_t end=std::min(pending.size(),single?size_t(1):batchTarget);
        std::vector<MazeTask> tasks;
        tasks.reserve(end);
        const size_t lookahead=std::min(pending.size(),guided?4*end:end);
        std::vector<int> skipped;
        std::unordered_set<Resource> occupied;
        for(size_t i=0;i<lookahead && tasks.size()<end;++i) {
            const int id=pending.front();
            if(!tasks.empty() && (stage.expired() || (retries.count(id) && retries.at(id)==maxRetries))) break;
            pending.pop_front();
            const auto& current=incumbent.current[id];
            if(guided && end>1) {
                const auto keys=contentionKeys(graph,id,current.summary);
                if(std::any_of(keys.begin(),keys.end(),[&](Resource r){return occupied.count(r);})) {
                    skipped.push_back(id);
                    continue;
                }
                occupied.insert(keys.begin(),keys.end());
            }
            tasks.push_back({id,current.version,current.summary,graph.readView(id)});
        }
        // 跳过的任务按原优先级留在队首，下一批至少接纳队首，避免饥饿。
        for(auto it=skipped.rbegin();it!=skipped.rend();++it) pending.push_front(*it);
        deferred+=skipped.size();
        std::vector<MazeResult> results(tasks.size());
        scheduleSeconds+=elapsed(scheduleStart);
        ++batches;
        const auto searchStart=Clock::now();
        pool->run(tasks.size(),[&](size_t i,size_t worker) {
            if(stage.expired()) return;
            const auto begin=Clock::now();
            const auto cpuStart=cpuSeconds();
            const auto& task=tasks[i];
            auto& result=results[i];
            result.started=true;
            result.wait=elapsed(searchStart);
            result.worker=worker;
            Deadline one(stage,netSeconds,&pool->cancellation());
            if(checks) one.limitChecks(checks);
            Profile::worker(profile,task.id,NetMethod::Maze,[&] {
                Measured measure(Timing::Stage3Maze);
                Routes routes;
                MazeEffort effort;
                try {routes=SparseMaze(graph,task.id,one,&task.view,&effort).candidates(stride,true);}
                catch(const SearchTimeout&) {result.timeout=true;Profile::count(Event::MazeTimeouts);}
                result.expansions=effort.expanded;
                result.refineTimeout=effort.refineTimeout;
                result.generated=routes.size();
                for(auto& route:routes) {
                    auto prepared=graph.prepareRoute(task.id,route);
                    if(task.previous && ((prepared->cost()+1e-9>=task.previous->cost() &&
                       !(incumbent.mode=="budgeted" && prepared->hbtSites().size()<task.previous->hbtSites().size())) ||
                       (protect && prepared->hbtSites().size()>task.previous->hbtSites().size()))) {
                        ++result.qualityRejected;
                        continue;
                    }
                    result.improvements.push_back({std::move(route),std::move(prepared)});
                }
                std::stable_sort(result.improvements.begin(),result.improvements.end(),
                    [](const Alternative& a,const Alternative& b){return a.summary->cost()<b.summary->cost();});
            });
            result.seconds=elapsed(begin);
            const double cpuEnd=cpuSeconds();
            result.cpu=cpuStart<0 || cpuEnd<0?-1.:cpuEnd-cpuStart;
        });
        searchWall+=elapsed(searchStart);
        const auto commitStart=Clock::now();
        std::set<size_t> workers;
        const size_t previousChecks=verified,previousConflicts=conflicts;
        for(size_t i=0;i<tasks.size();++i) {
            const auto& task=tasks[i];
            auto& r=results[i];
            if(!r.started) {++notStarted;continue;}
            ++attempted;
            retryCount+=retries.count(task.id)>0;
            workers.insert(r.worker);
            generated+=r.generated;
            quality+=r.qualityRejected;
            expired+=r.timeout;
            refineExpired+=r.refineTimeout;
            searchFailed+=!r.generated && !r.timeout;
            completed+=!r.timeout;
            workerElapsed+=r.seconds;
            queueWait+=r.wait;
            expansions+=r.expansions;
            if(r.cpu<0) cpuKnown=false;else workerCPU+=r.cpu;
            auto& state=incumbent.current[task.id];
            if(task.version!=state.version) {++stale;continue;}
            if(incumbent.mode=="budgeted") {
                auto rank=[&](const Alternative& a){return std::make_pair(
                    incumbent.excess(incumbent.after(state.summary,a.summary)),a.summary->cost());};
                std::stable_sort(r.improvements.begin(),r.improvements.end(),[&](const Alternative& a,const Alternative& b){return rank(a)<rank(b);});
            }
            bool changed=false,resourceFailure=false;
            for(auto& alt:r.improvements) {
                if(!incumbent.improves(state.summary,alt.summary,state.summary)) {
                    if(incumbent.excess(incumbent.after(state.summary,alt.summary))>incumbent.excess(incumbent.hbtTotal))
                        ++budgetRejected;
                    else ++quality;
                    continue;
                }
                ++verified;
                if(incumbent.replace(task.id,task.version,std::move(alt.route),alt.summary)) {
                    ++accepted;
                    state.failures=0;
                    changed=true;
                    break;
                }
                ++conflicts;
                resourceFailure=true;
            }
            if(!changed) {
                if(r.timeout || !r.generated) ++state.failures;
                restored+=bool(state.summary);
                if(resourceFailure) {
                    int& usedRetries=retries[task.id];
                    if(usedRetries<maxRetries) {
                        ++usedRetries;
                        ++queuedRetries;
                        pending.push_back(task.id);
                    } else ++unresolved;
                }
            }
        }
        peakWorkers=std::max(peakWorkers,workers.size());
        if(adapt && !single && verified>previousChecks) {
            const double rate=double(conflicts-previousConflicts)/(verified-previousChecks);
            size_t next=batchTarget;
            if(rate>high) next=std::max<size_t>(1,batchTarget/2);
            else if(rate<low) next=std::min(2*initialTarget,batchTarget+count);
            shrank+=next<batchTarget;
            grew+=next>batchTarget;
            batchTarget=next;
        }
        commitSeconds+=elapsed(commitStart);
    }
    return {{"requested_threads",requested},{"worker_limit",workerLimit},{"actual_workers",peakWorkers},{"maze_batches",batches},
            {"maze_tasks",attempted},{"completed",completed},{"accepted",accepted},{"restored",restored},
            {"candidates",generated},{"maze_search_timeouts",expired},{"maze_refinement_timeouts",refineExpired},
            {"search_failed",searchFailed},{"maze_search_wall_seconds",searchWall},
            {"maze_worker_elapsed_seconds",workerElapsed},{"maze_worker_cpu_seconds",cpuKnown?Json(workerCPU):Json(nullptr)},
            {"queue_wait_sum_seconds",queueWait},{"scheduler_seconds",scheduleSeconds},{"commit_seconds",commitSeconds},
            {"quality_rejections",quality},{"budget_rejections",budgetRejected},{"resource_conflict_rejections",conflicts},{"actual_commit_checks",verified},
            {"resource_conflict_rate",verified?Json(double(conflicts)/verified):Json(nullptr)},
            {"retry_count",retryCount},{"retries_queued",queuedRetries},{"serial_fallback_count",serialFallback},{"unresolved_conflicts",unresolved},
            {"guided",guided},{"adaptive",adapt},{"conflict_deferrals",deferred},{"batch_increases",grew},{"batch_decreases",shrank},
            {"pending_at_deadline",pending.size()+notStarted},{"stale_candidate_rejections",stale},{"astar_expanded_nodes",expansions},{"stage_overrun_seconds",stage.overrun()}};
}
}
