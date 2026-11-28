#include "MazeExperiment.h"
#include "SparseMaze.h"
#include "WorkerPool.h"
#include <boost/archive/binary_iarchive.hpp>
#include <boost/archive/binary_oarchive.hpp>
#include <boost/serialization/unordered_map.hpp>
#include <boost/serialization/vector.hpp>
#include <boost/serialization/utility.hpp>
#include <numeric>

namespace native3d {
namespace {
struct Observation {
    std::vector<Resource> signature;
    uint64_t expansions=0,checks=0;
    size_t candidates=0;
    bool timedOut=false,refineTimeout=false;
    bool operator==(const Observation& b) const {
        return signature==b.signature && expansions==b.expansions && checks==b.checks &&
               candidates==b.candidates && timedOut==b.timedOut && refineTimeout==b.refineTimeout;
    }
};
std::string protocol(const RoutingGraph& g) {
    const auto& cfg=g.model.config;
    Json identity;
    for(const auto key:{"dies","mapping","bonding","bonded_dbu","weights","enable_hbt_footprint"})
        if(cfg.count(key)) identity[key]=cfg.at(key);
    if(cfg.count("provenance") && cfg.at("provenance").count("input_sha256"))
        identity["input_sha256"]=cfg.at("provenance").at("input_sha256");
    return identity.dump();
}
template<class Archive,class Point> void point(Archive& ar,Point& p) {ar & p.dieIdx & p.layerIdx & p.x & p.y;}
void writeRoute(boost::archive::binary_oarchive& ar,const Route& route) {
    point(ar,route.root);
    ar<<route.edges.size();
    for(const auto& e:route.edges) {point(ar,e.a);point(ar,e.b);ar<<e.type<<e.site;}
}
Route readRoute(boost::archive::binary_iarchive& ar,const RoutingGraph& graph) {
    Route route{{0,0,0},{}};
    point(ar,route.root);
    size_t count;
    ar>>count;
    if(count>graph.nodeCount()) throw std::invalid_argument("Checkpoint tree edge count exceeds grid");
    for(size_t i=0;i<count;++i) {
        GRPoint a(0,0,0),b(0,0,0);
        GREdgeType type;
        int site;
        point(ar,a);point(ar,b);ar>>type>>site;
        if((type!=GREdgeType::WIRE && type!=GREdgeType::VIA && type!=GREdgeType::HBT) ||
           !route.edges.emplace(a,b,type,site).second) throw std::invalid_argument("Invalid checkpoint edge");
    }
    return route;
}
void saveCheckpoint(const IncumbentSolution& solution,const std::vector<std::pair<double,int>>& priority,
                    const std::string& path,bool stage2) {
    std::ofstream file;
    file.exceptions(std::ios::badbit|std::ios::failbit);
    file.open(path,std::ios::binary);
    {
        boost::archive::binary_oarchive ar(file);
        ar<<std::string(stage2?"CUGR3D-STAGE2-1":"CUGR3D-MAZE-1")<<protocol(solution.graph)<<solution.current.size();
        for(const auto& entry:solution.current) {
            ar<<entry.version<<entry.failures<<bool(entry.summary);
            if(entry.summary) writeRoute(ar,entry.route);
            if(stage2) {
                ar<<entry.alternatives.size();
                for(const auto& route:entry.alternatives) writeRoute(ar,route);
            }
        }
        ar<<priority<<solution.graph.demand()<<solution.graph.searchPrices();
    }
    file.close();
}
std::vector<std::pair<double,int>> loadCheckpoint(IncumbentSolution& solution,const std::string& path,bool stage2) {
    std::ifstream file(path,std::ios::binary);
    if(!file) throw std::runtime_error("Cannot open maze checkpoint: "+path);
    boost::archive::binary_iarchive ar(file);
    std::string version,input;
    size_t nets;
    ar>>version>>input>>nets;
    if(version!=(stage2?"CUGR3D-STAGE2-1":"CUGR3D-MAZE-1") || input!=protocol(solution.graph) || nets!=solution.current.size())
        throw std::invalid_argument("Checkpoint stage or physical protocol differs");
    for(size_t id=0;id<nets;++id) {
        unsigned revision,failures;bool present;
        ar>>revision>>failures>>present;
        auto& state=solution.current[id];
        if(present) {
            auto route=readRoute(ar,solution.graph);
            auto summary=solution.graph.prepareRoute(id,route);
            solution.publish(id,std::move(route),summary);
        }
        if(stage2) {
            size_t count;
            ar>>count;
            if(count>128) throw std::invalid_argument("Too many Stage 2 checkpoint alternatives");
            for(size_t i=0;i<count;++i) {
                auto route=readRoute(ar,solution.graph);
                const auto summary=solution.graph.prepareRoute(id,route);
                if(!solution.graph.staticallyAvailable(*summary)) throw std::invalid_argument("Illegal checkpoint alternative");
                state.alternatives.push_back(std::move(route));
            }
        }
        state.version=revision;state.failures=failures;
    }
    std::vector<std::pair<double,int>> priority;
    Occupancy demand,prices;
    ar>>priority>>demand>>prices;
    std::set<int> unique;
    for(const auto& item:priority) if(item.second<0 || size_t(item.second)>=nets || !unique.insert(item.second).second)
        throw std::invalid_argument("Bad checkpoint task identity");
    for(const auto& item:prices) if(!std::isfinite(item.second) || item.second<0) throw std::invalid_argument("Bad checkpoint price");
    solution.graph.restoreCheckpointDemand(demand);
    solution.graph.searchPrices(std::move(prices));
    return priority;
}
}
void saveMazeCheckpoint(const IncumbentSolution& solution,const std::vector<std::pair<double,int>>& priority,
                        const std::string& path) {
    saveCheckpoint(solution,priority,path,false);
}
std::vector<std::pair<double,int>> loadMazeCheckpoint(IncumbentSolution& solution,const std::string& path) {
    return loadCheckpoint(solution,path,false);
}
void saveStage2Checkpoint(const IncumbentSolution& solution,const std::string& path) {
    saveCheckpoint(solution,{},path,true);
}
void loadStage2Checkpoint(IncumbentSolution& solution,const std::string& path) {
    loadCheckpoint(solution,path,true);
}
Json measureMazeSearch(const IncumbentSolution& solution,const std::vector<std::pair<double,int>>& priority,int stride) {
    const auto& graph=solution.graph;
    const auto& cfg=graph.model.config.at("maze_work_study");
    const int taskLimit=integer(cfg.at("tasks")),checks=integer(cfg.at("checks")),repeats=integer(cfg.at("repeats"));
    if(taskLimit<1 || checks<1 || repeats<1 || repeats>100) throw std::invalid_argument("Invalid maze study limits");
    std::vector<int> widths;
    for(const auto& item:cfg.at("threads")) {
        int width=integer(item);
        if(width<1 || width>graph.parameters().threads || std::find(widths.begin(),widths.end(),width)!=widths.end())
            throw std::invalid_argument("Invalid study thread count");
        widths.push_back(width);
    }
    if(widths.empty() || widths.front()!=1) throw std::invalid_argument("First maze study run must be serial");
    const size_t n=std::min<size_t>(priority.size(),taskLimit);
    std::vector<Observation> reference;
    Json result;
    result["tasks"]=Json::array();
    for(size_t i=0;i<n;++i) result["tasks"].push_back(priority[i].second);
    result["checks_per_net"]=checks;
    for(int r=0;r<repeats;++r) {
        // 交替执行顺序，使热缓存和机器负载不总偏向同一并行度。
        if(r) std::reverse(widths.begin(),widths.end());
        for(const int width:widths) {
            std::vector<Observation> observations(n);
            auto calculate=[&](size_t i) {
                const int net=priority[i].second;
                const auto view=graph.readView(net);
                Deadline deadline(3600.);
                deadline.limitChecks(checks);
                auto& obs=observations[i];
                MazeEffort work;
                Routes candidates;
                try {candidates=SparseMaze(graph,net,deadline,&view,&work).candidates(stride,true);}
                catch(const SearchTimeout&) {obs.timedOut=true;}
                if(deadline.expired()) throw std::runtime_error("Maze study exceeded wall-clock guard");
                obs.checks=deadline.checks();
                obs.expansions=work.expanded;
                obs.refineTimeout=work.refineTimeout;
                obs.candidates=candidates.size();
                for(const auto& route:candidates) {
                    graph.validate(net,route,&view);
                    obs.signature.push_back(graph.nodeId(route.root));
                    obs.signature.push_back(route.edges.size());
                    for(const auto& e:route.edges) {
                        obs.signature.push_back(graph.resourceId(e));
                        obs.signature.push_back(graph.nodeId(e.a));
                        obs.signature.push_back(graph.nodeId(e.b));
                    }
                }
            };
            const auto begin=std::chrono::steady_clock::now();
            if(width==1) {
                for(size_t i=0;i<n;++i) calculate(i);
            } else if(n) {
                WorkerPool workers(std::min<size_t>(width,n));
                workers.run(n,[&](size_t i,size_t){calculate(i);});
            }
            const double seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-begin).count();
            uint64_t expanded=0,usedChecks=0,generated=0,seedTimeouts=0,refineTimeouts=0;
            for(const auto& o:observations) {
                expanded+=o.expansions;usedChecks+=o.checks;generated+=o.candidates;
                seedTimeouts+=o.timedOut;refineTimeouts+=o.refineTimeout;
            }
            if(r==0 && width==1) reference=std::move(observations);
            else if(reference!=observations) throw std::logic_error("Fixed-work maze candidates or search counts differ");
            result["runs"].push_back({{"repeat",r},{"threads",width},{"seconds",seconds},{"tasks",n},
                {"checks",usedChecks},{"astar_expanded_nodes",expanded},{"candidates",generated},
                {"seed_work_limits",seedTimeouts},{"refine_work_limits",refineTimeouts},{"equal_to_serial",true}});
        }
    }
    return result;
}
}
