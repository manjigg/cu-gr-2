#include "SparseMaze.h"
#include "Profile.h"
#include <queue>

namespace native3d {
void MazeEffort::observe(const std::vector<Resource>& resources) {
    // Misra–Gries 有界热点计数仅用于选择相关线网，不能证明不可布通。
    for(auto id:resources) {
        if(blocked.count(id) || blocked.size()<16) {++blocked[id];continue;}
        for(auto it=blocked.begin();it!=blocked.end();) {
            if(--it->second==0) it=blocked.erase(it);else ++it;
        }
    }
}
namespace {
class MazeWorkspace {
    struct Label {
        uint64_t generation=0;
        double price=0;
        GRPoint parent{0,0,0};
        GREdgeType type=GREdgeType::WIRE;
        int site=-1;
    };
    std::vector<Label> dense;
    std::unordered_map<Resource,Label> sparse;
    uint64_t generation=0;
    bool direct=false;
public:
    void next(Resource gridSize,bool useDense) {
        direct=useDense && gridSize<=400000;
        ++generation;
        if(!generation) {dense.clear();++generation;}
        if(direct && dense.size()<gridSize) dense.resize(gridSize);
        if(!direct) sparse.clear();
    }
    double distance(Resource id) const {
        if(direct) {
            const auto& label=dense[id];
            if(label.generation==generation) return label.price;
        } else {
            const auto at=sparse.find(id);
            if(at!=sparse.end()) return at->second.price;
        }
        return std::numeric_limits<double>::infinity();
    }
    void write(Resource id,double price,GRPoint parent,GREdgeType type=GREdgeType::WIRE,int site=-1) {
        (direct?dense[id]:sparse[id])={generation,price,parent,type,site};
    }
    Edge back(Resource id,GRPoint point) const {
        const auto& label=direct?dense[id]:sparse.at(id);
        if(label.generation!=generation) throw std::logic_error("Stale maze predecessor");
        return Edge(point,label.parent,label.type,label.site);
    }
};
}
bool SparseMaze::grow(Route& tree,int stride,bool allowDieDetour,bool bounded) const {
    std::set<int> lines[2][2];
    auto mark=[&](GRPoint p) {
        lines[p.dieIdx][0].insert(p.x);
        lines[p.dieIdx][1].insert(p.y);
    };
    const auto& pins=graph.terminals(net);
    bool cross=false;
    for(const auto& pin:pins) for(auto p:pin) {
        mark(p);
        cross|=p.dieIdx!=tree.root.dieIdx;
    }
    auto onLine=[&](int die,int axis,int position) {
        return lines[die][axis].count(position) || ((cross||allowDieDetour) &&
            graph.landingLines(die,axis).count(position));
    };
    for(int d=0;d<2;++d) for(int a=0;a<2;++a) {
        int size=graph.model.dies[d]->grid.getSize(a);
        for(int i=0;i<size;i+=stride) lines[d][a].insert(i);
        lines[d][a].insert(size-1);
    }
    std::set<GRPoint> connected{tree.root};
    // 同一轮接树遭遇组合占用冲突时，重新搜索另一条回溯分支。
    Edges excluded;
    RTree vertexIndex;
    std::vector<GRPoint> indexPoints;
    auto indexVertex=[&](GRPoint p) {
        auto xy=graph.center(p);boostPoint q(xy[0],xy[1]);
        vertexIndex.insert({boostBox(q,q),int(indexPoints.size())});indexPoints.push_back(p);
    };
    indexVertex(tree.root);
    const bool cached=graph.model.config.value("routing_mode",std::string("exact"))=="fast" &&
                      graph.model.config.value("enable_maze_workspace",true);
    thread_local MazeWorkspace workspace;
    Demand treeDemand;
    std::vector<int> chosenSites;
    XY treeLow=graph.center(tree.root),treeHigh=treeLow;
    while(true) {
        time.check();
        std::set<GRPoint> targets;
        const std::vector<GRPoint>* nextPin=nullptr;
        double nearest=std::numeric_limits<double>::infinity();
        for(const auto& pin:pins) {
            if(std::any_of(pin.begin(),pin.end(),[&](GRPoint p){return connected.count(p);})) continue;
            if(pins.size()<=2) {targets.insert(pin.begin(),pin.end());continue;}
            // 按到现有树的距离挑选下一个真实终端，再从树的所有节点共同出发。
            // 避免高扇出网的巨大目标包围盒将 A* 退化为全芯片洪泛。
            for(auto p:pin) {
                auto xy=graph.center(p);
                auto it=vertexIndex.qbegin(bgi::nearest(boostPoint(xy[0],xy[1]),1));
                auto q=indexPoints[it->second];auto loc=graph.center(q);
                double score=(std::abs(loc[0]-xy[0])+std::abs(loc[1]-xy[1]))*graph.minimumCostPerBondDbu()+
                    graph.layerDistance(graph.stateIndex(q),graph.stateIndex(p));
                if(!nextPin||score<nearest) {nearest=score;nextPin=&pin;}
            }
        }
        if(nextPin) targets.insert(nextPin->begin(),nextPin->end());
        if(targets.empty()) break;
        bool accessible=false;
        for(const auto& p:targets) {
            for(const auto& e:graph.neighbors(p,net)) {
                if(!cross && !allowDieDetour && e.type==GREdgeType::HBT) continue;
                if(graph.available(net,{e},view)) {accessible=true;break;}
                if(effort && effort->recordBlockers) effort->observe(graph.blockingResources(net,e,view));
            }
            if(accessible) break;
        }
        if(!accessible) return false;
        XY lo=graph.center(*targets.begin()),hi=lo;
        const int nLayers=graph.model.dies[0]->grid.getNumLayers()+graph.model.dies[1]->grid.getNumLayers();
        std::vector<double> layerBound(nLayers,std::numeric_limits<double>::infinity());
        std::set<int> targetLayers;
        for(auto target:targets) {
            targetLayers.insert(graph.stateIndex(target));
            auto xy=graph.center(target);
            for(int axis:{0,1}) {lo[axis]=std::min(lo[axis],xy[axis]);hi[axis]=std::max(hi[axis],xy[axis]);}
        }
        for(int from=0;from<nLayers;++from) for(int to:targetLayers)
            layerBound[from]=std::min(layerBound[from],graph.layerDistance(from,to));
        // 任何边的代价至少是其 bonded L1 位移乘 distancePrice，
        // 包含两侧网格错位导致的 HBT 格中心跳跃。到目标包围盒的距离是一致下界。
        auto remaining=[&](GRPoint p) {
            auto xy=graph.center(p);int64_t distance=0;
            for(int a:{0,1}) distance+=std::max<int64_t>({0,lo[a]-xy[a],xy[a]-hi[a]});
            return distance*graph.minimumCostPerBondDbu()+layerBound[graph.stateIndex(p)];
        };
        auto inside=[&](GRPoint p) {
            if(!bounded) return true;
            const auto xy=graph.center(p);
            const auto margin=int64_t(graph.model.units)*12;
            for(unsigned a:{0,1})
                if(xy[a]<std::min(lo[a],treeLow[a])-margin || xy[a]>std::max(hi[a],treeHigh[a])+margin) return false;
            return true;
        };
        using Item=std::tuple<double,double,GRPoint>;
        std::priority_queue<Item,std::vector<Item>,std::greater<Item>> queue;
        workspace.next(graph.nodeCount(),cached);
        for(auto p:connected) {workspace.write(graph.nodeId(p),0,p);queue.emplace(remaining(p),0,p);}
        bool reached=false,restart=false;
        while(!queue.empty()) {
            time.check();
            Item current=queue.top();
            queue.pop();
            const auto u=std::get<2>(current);
            const auto from=-std::get<1>(current);
            if(from!=workspace.distance(graph.nodeId(u))) continue;
            if(targets.count(u)) {
                Edges path;
                GRPoint v=u;
                while(!connected.count(v)) {
                    const auto e=workspace.back(graph.nodeId(v),v);
                    path.insert(e);
                    v=(v==e.a?e.b:e.a);
                }
                Edges combined;
                if(!cached) {
                    combined=tree.edges;
                    combined.insert(path.begin(),path.end());
                }
                // 单条边合法不保证整段路径的 Via 金属需求可容纳。
                const bool valid=cached?graph.extendPath(net,path,tree.edges,treeDemand,chosenSites,view):
                                        graph.available(net,combined,view);
                if(!valid) {
                    Profile::count(Event::MazeRetries);
                    Profile::retry();
                    Edges trial=tree.edges;
                    for(const auto& e:path) {
                        trial.insert(e);
                        if(!graph.available(net,trial,view)) {
                            if(!excluded.insert(e).second) throw std::logic_error("Repeated excluded branch");
                            restart=true;break;
                        }
                    }
                    if(!restart) throw std::logic_error("Missing conflicting edge");
                    break;
                }
                for(const auto& e:path) for(auto p:{e.a,e.b})
                    if(connected.insert(p).second) {
                        indexVertex(p);
                        const auto xy=graph.center(p);
                        for(unsigned axis:{0,1}) {
                            treeLow[axis]=std::min(treeLow[axis],xy[axis]);
                            treeHigh[axis]=std::max(treeHigh[axis],xy[axis]);
                        }
                    }
                excluded.clear();
                if(!cached) tree.edges.swap(combined);
                reached=true;break;
            }
            if(effort) ++effort->expanded;
            Profile::count(Event::MazeExpansions);
            for(const auto& e:graph.neighbors(u,net)) {
                if(e.type==GREdgeType::HBT && !cross && !allowDieDetour) continue;
                if(e.type==GREdgeType::WIRE) {
                    const int dir=graph.model.dies[u.dieIdx]->grid.getLayerDirection(u.layerIdx);
                    if(!onLine(u.dieIdx,1-dir,u[1-dir])) continue;
                } else if(e.type==GREdgeType::VIA &&
                    (!onLine(u.dieIdx,0,u.x)||!onLine(u.dieIdx,1,u.y))) continue;
                auto v=(u==e.a?e.b:e.a);
                if(connected.count(v) || excluded.count(e) || !inside(v)) continue;
                double step=graph.stepCost(net,e,view);
                if(!std::isfinite(step)) {
                    if(effort && effort->recordBlockers) effort->observe(graph.blockingResources(net,e,view));
                    continue;
                }
                double next=from+step;
                const auto id=graph.nodeId(v);
                if(workspace.distance(id)<=next) continue;
                workspace.write(id,next,u,e.type,e.site);
                queue.emplace(next+remaining(v),-next,v);
            }
        }
        if(restart) continue;
        if(!reached) return false;
    }
    try {graph.validate(net,tree,view);} catch(const std::invalid_argument&) {return false;}
    return true;
}
Routes SparseMaze::seed(int stride,bool allowDieDetour) const {
    Measured measured(Timing::Maze);
    if(stride<1) throw std::invalid_argument("Sparse pitch must be positive");
    const auto& pins=graph.terminals(net);
    if(pins.empty()) throw std::invalid_argument("Empty net");
    const auto& cfg=graph.model.config;
    const bool window=cfg.value("routing_mode",std::string("exact"))=="fast" && cfg.value("enable_bounded_maze",true);
    while(true) {
        for(auto source:pins.front()) {
            Route tree{source,{}};
            if(window && grow(tree,stride,allowDieDetour,true)) return {std::move(tree)};
            tree={source,{}};
            if(grow(tree,stride,allowDieDetour,false)) return {std::move(tree)};
        }
        if(stride==1) {Profile::count(Event::MazeFailed);return {};}
        Profile::retry();
        Profile::count(Event::MazeRetries);
        stride=std::max(1,stride/2);
    }
}
Routes SparseMaze::candidates(int stride,bool allowDieDetour) const {
    auto found=seed(stride,allowDieDetour);
    if(found.empty()) return {};
    try {Measured refine(Timing::MazeRefine);return PatternSearch(graph,net,time).refineMaze(found.front(),view);}
    catch(const SearchTimeout&) {
        if(graph.model.config.value("routing_mode",std::string("exact"))=="exact") throw;
        Profile::retry();
        if(effort) effort->refineTimeout=true;
        Profile::count(Event::RefineTimeouts);
        return found;
    }
}
}
