#include "gr/GlobalRouter.h"
#include "gr/PatternRoute.h"
#include "GuideOutput.h"
#include "IncrementalRoute.h"
#include "SparseMaze.h"
#include "SeedTopology.h"
#include "Profile.h"
#include <boost/filesystem.hpp>
#include <sys/resource.h>

namespace native3d {
static void extend(Routes& pool,Routes batch) {
    std::set<Edges> seen;
    for(const auto& r:pool) seen.insert(r.edges);
    for(auto& r:batch) if(seen.insert(r.edges).second) pool.push_back(std::move(r));
}
// 分组发布失败时回滚本次已提交的每一根网。
static void commitAll(RoutingGraph& graph,const std::vector<Route>& trees) {
    size_t i=0;
    try {for(;i<trees.size();++i) graph.commit(i,trees[i]);}
    catch(...) {while(i>0) graph.release(--i);throw;}
}
static std::vector<int> requestedStages(const Json& config) {
    std::vector<int> stages{1,2,3};
    if(config.count("stages")) {
        auto configured=config.at("stages");
        if(!configured.is_array()||configured.empty()) throw std::invalid_argument("Expected a nonempty stage sequence");
        stages.clear();
        for(const auto& s:configured) {
            int n=integer(s);
            if(n<1||n>3||(!stages.empty()&&n<=stages.back())) throw std::invalid_argument("Invalid stage order");
            stages.push_back(n);
        }
    }
    return stages;
}
}
int GlobalRouter::route(native3d::MultiDieDesign& model,const std::string& output) {
    using namespace native3d;
    namespace fs=boost::filesystem;
    using Clock=std::chrono::steady_clock;
    if(output.empty()) throw std::invalid_argument("Specify -output with a new or empty directory");
    fs::create_directories(output);
    if(!fs::is_empty(output)) throw std::invalid_argument("Output directory already contains files: "+output);
    auto begin=Clock::now();
    Json report;
    report["inventory"]=model.inventory();
    report["metrics"]=nullptr;report["detailed_drc"]=nullptr;
    auto initialize=[&]() {
        Measured measured(Timing::Initialization);
        return RoutingGraph(model);
    };
    RoutingGraph graph=initialize();
    if(auto* profile=Profile::current()) profile->prepareNets(model.nets.size());
    // FLUTE C 内核按工作目录读取表。在单线程初始化期间切到随构建提供的表目录。
    auto working=fs::current_path();
    if(!fs::exists("POWV9.dat") || !fs::exists("POST9.dat")) {
        const char* overrideDir=std::getenv("CUGR_FLUTE_LUT");
        fs::current_path(overrideDir?overrideDir:CUGR_LUT_DIR);
    }
    if(!fs::is_regular_file("POWV9.dat") || !fs::is_regular_file("POST9.dat"))
        throw std::runtime_error("Missing FLUTE lookup tables");
    PatternRoute::readFluteLUT();
    fs::current_path(working);
    const auto stages=requestedStages(model.config);
    const auto method=model.config.value("routing_mode",std::string("exact"));
    if(method!="exact" && method!="fast") throw std::invalid_argument("Unknown routing mode: "+method);
    std::vector<Route> incumbent;
    bool allFinished=true;
    if(method=="fast") {
        auto optimized=routeIncrementally(graph,stages,report);
        report["unrouted_net_ids"]=std::move(optimized.unrouted);
        incumbent.swap(optimized.routes);
    } else {
        std::vector<Routes> pool(model.nets.size());
        // 先构造可行上界。该贪心初解只增加候选，不能用于证明最优或不可行。
        std::vector<int> committed;
        const double seedSeconds=model.config.value("seed_seconds",0.);
        if(seedSeconds>0) {
            Measured measured(Timing::Seed);
            Deadline seedTime(seedSeconds);
            const int stride=model.config.value("sparse_stride",4);
            std::vector<int> order;
            for(const auto& n:model.nets) order.push_back(n.id);
            std::stable_sort(order.begin(),order.end(),[&](int a,int b) {
                bool localA=model.nets[a].localNet[0]<0||model.nets[a].localNet[1]<0;
                bool localB=model.nets[b].localNet[0]<0||model.nets[b].localNet[1]<0;
                return localA>localB;
            });
            size_t attempted=0;
            try {
                for(int n:order) {
                    seedTime.check();++attempted;
                    MeasuredNet measureNet(n,NetMethod::Seed);
                    const auto started=Clock::now();
                    auto r=initialCandidates(graph,n,seedTime);
                    if(r.empty()) {
                        Profile::retry();
                        MeasuredNet mazeNet(n,NetMethod::Maze);
                        r=SparseMaze(graph,n,seedTime).seed(stride,false);
                    }
                    const double elapsed=std::chrono::duration<double>(Clock::now()-started).count();
                    if(elapsed>=1. || r.empty())
                        std::cout<<"NATIVE_SEED_NET "<<n<<" pins="<<graph.terminals(n).size()
                                 <<" seconds="<<elapsed<<" candidates="<<r.size()<<std::endl;
                    if(r.empty()) continue;
                    graph.commit(n,r.front());committed.push_back(n);pool[n]=std::move(r);
                }
            } catch(const SearchTimeout&) {report["seed_time_limit"]=true;}
            report["seed_failed_net_ids"]=Json::array();
            for(size_t i=0;i<attempted;++i) if(pool[order[i]].empty()) report["seed_failed_net_ids"].push_back(order[i]);
            report["seed_attempted"]=attempted;report["seed_feasible_nets"]=committed.size();
            for(int n:committed) graph.release(n);
            if(committed.size()==model.nets.size()) for(const auto& r:pool) incumbent.push_back(r.front());
            commitAll(graph,incumbent);
            std::cout<<"NATIVE_SEED "<<committed.size()<<" / "<<model.nets.size()<<std::endl;
        }
        std::vector<unsigned> previous(incumbent.size(),0);
        report["bound_scope"]="generated_candidate_pool";
        report["unrestricted_optimality_gap"]=nullptr;
        for(int stage:stages) {
            Measured stageProfile(stage==1?Timing::Stage1:stage==2?Timing::Stage2:Timing::Stage3);
            auto start=Clock::now();
            Deadline limit(model.config.value("stage_seconds",60.0));
            Json rec;
            rec["stage"]=stage;rec["best_feasible"]=nullptr;rec["lower_bound"]=nullptr;
            rec["gap"]=nullptr;rec["candidate_nets_completed"]=0;
            rec["candidate_generation"]="RUNNING";rec["solver_status"]="NOT_RUN";
            if(stage==2) graph.captureCongestion();
            // 旧解在生成与选择期间保留；异常时恢复对同一个共享图的所有权。
            std::vector<Route> saved;
            saved.swap(incumbent);
            for(size_t i=0;i<saved.size();++i) graph.release(i);
            try {
                try {
                    Measured generation(Timing::Candidates);
                    for(size_t i=0;i<pool.size();++i) {
                        MeasuredNet measureNet(i,stage==1?NetMethod::Pattern:stage==2?NetMethod::Detour:NetMethod::Maze);
                        limit.check();
                        Routes extra;
                        if(stage==3) extra=SparseMaze(graph,i,limit).candidates(
                            model.config.value("sparse_stride",4),model.config.value("same_die_maze_detour",false));
                        else extra=PatternSearch(graph,i,limit).enumerate(stage==2);
                        extend(pool[i],std::move(extra));
                        rec["candidate_nets_completed"]=i+1;
                    }
                    rec["candidate_generation"]="FINISHED";
                } catch(const SearchTimeout&) {rec["candidate_generation"]="TIME_LIMIT";allFinished=false;}
                // 当前候选池与旧解处于相同的外部占用状态；阶段超时保留可行的旧树。
                if(std::all_of(pool.begin(),pool.end(),[](const Routes& r){return !r.empty();}) ||
                   rec["candidate_generation"]=="FINISHED") {
                    Deadline solveTime(model.config.value("master_seconds",model.config.value("stage_seconds",60.0)));
                    auto best=selectRoutes(graph,pool,solveTime,previous);
                    rec["solver_status"]=best.status;
                    if(best.status=="TIME_LIMIT") allFinished=false;
                    if(std::isfinite(best.lower)) rec["lower_bound"]=best.lower;
                    if(std::isfinite(best.objective)) {
                        std::vector<Route> selected;
                        for(size_t i=0;i<pool.size();++i) selected.push_back(pool[i].at(best.selection.at(i)));
                        incumbent.swap(selected);previous=best.selection;
                    }
                    const auto& active=incumbent.empty()?saved:incumbent;
                    if(!active.empty()) {
                        double objective=0;
                        for(const auto& r:active) objective+=graph.cost(r.edges);
                        rec["best_feasible"]=objective;
                        if(std::isfinite(best.lower)) rec["gap"]=(objective==0?0:std::max(0.,(objective-best.lower)/objective));
                    }
                }
                commitAll(graph,incumbent.empty()?saved:incumbent);
            } catch(...) {
                commitAll(graph,saved);
                throw;
            }
            if(incumbent.empty()) incumbent.swap(saved);
            size_t count=0;
            for(const auto& p:pool) count+=p.size();
            rec["pool_size"]=count;
            rec["seconds"]=std::chrono::duration<double>(Clock::now()-start).count();
            report["stages"].push_back(rec);
            std::cout<<"NATIVE_STAGE "<<rec.dump()<<std::endl;
        }
    }
    if(!incumbent.empty()) {
        Measured outputTimer(Timing::Output);
        report["metrics"]=writeSolution(graph,incumbent,output);
    }
    if(auto* profile=Profile::current()) profile->describeNets(model,graph,incumbent);
    if(method=="fast") report["status"]=incumbent.empty()?"NO_FEASIBLE_FAST_ROUTE":"FEASIBLE_FAST";
    else report["status"]=incumbent.empty()?(allFinished?"INFEASIBLE_CANDIDATE_POOL":"NO_INCUMBENT_TIME_LIMIT"):
                       (allFinished?"OPTIMAL_CANDIDATE_POOL":"FEASIBLE_TIME_LIMIT");
    report["routing_seconds"]=std::chrono::duration<double>(Clock::now()-begin).count();
    struct rusage usage{};
    if(getrusage(RUSAGE_SELF,&usage)==0) report["router_max_rss_kb"]=usage.ru_maxrss;
    std::ofstream os;
    os.exceptions(std::ios::badbit|std::ios::failbit);
    os.open(output+"/routing_statistics.json");os<<report.dump(2)<<'\n';os.close();
    return incumbent.empty()?2:0;
}
