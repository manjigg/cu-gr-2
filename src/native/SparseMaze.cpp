#include "SparseMaze.h"
#include "Profile.h"
#include <queue>

namespace native3d {
bool SparseMaze::grow(Route& tree,int stride,bool allowDieDetour) const {
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
            for(const auto& e:graph.neighbors(p,net))
                if((cross||allowDieDetour||e.type!=GREdgeType::HBT) && graph.available(net,{e})) {
                    accessible=true;break;
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
        using Item=std::tuple<double,double,GRPoint>;
        std::priority_queue<Item,std::vector<Item>,std::greater<Item>> queue;
        std::map<GRPoint,double> distance;
        std::map<GRPoint,Edge> back;
        for(auto p:connected) {distance[p]=0;queue.emplace(remaining(p),0,p);}
        bool reached=false,restart=false;
        while(!queue.empty()) {
            time.check();
            Item current=queue.top();
            queue.pop();
            const auto u=std::get<2>(current);
            const auto from=-std::get<1>(current);
            if(from!=distance.at(u)) continue;
            if(targets.count(u)) {
                Edges path;
                GRPoint v=u;
                while(!connected.count(v)) {
                    const auto& e=back.at(v);
                    path.insert(e);
                    v=(v==e.a?e.b:e.a);
                }
                auto combined=tree.edges;
                combined.insert(path.begin(),path.end());
                // 单条边合法不保证整段路径的 Via 金属需求可容纳。
                if(!graph.available(net,combined)) {
                    Profile::retry();
                    Edges trial=tree.edges;
                    for(const auto& e:path) {
                        trial.insert(e);
                        if(!graph.available(net,trial)) {
                            if(!excluded.insert(e).second) throw std::logic_error("Repeated excluded branch");
                            restart=true;break;
                        }
                    }
                    if(!restart) throw std::logic_error("Missing conflicting edge");
                    break;
                }
                for(const auto& e:path) for(auto p:{e.a,e.b})
                    if(connected.insert(p).second) indexVertex(p);
                excluded.clear();
                tree.edges.swap(combined);
                reached=true;break;
            }
            Profile::count(Event::MazeExpansions);
            for(const auto& e:graph.neighbors(u,net)) {
                if(e.type==GREdgeType::HBT && !cross && !allowDieDetour) continue;
                if(e.type==GREdgeType::WIRE) {
                    const int dir=graph.model.dies[u.dieIdx]->grid.getLayerDirection(u.layerIdx);
                    if(!onLine(u.dieIdx,1-dir,u[1-dir])) continue;
                } else if(e.type==GREdgeType::VIA &&
                    (!onLine(u.dieIdx,0,u.x)||!onLine(u.dieIdx,1,u.y))) continue;
                auto v=(u==e.a?e.b:e.a);
                if(connected.count(v) || excluded.count(e)) continue;
                double step=graph.stepCost(net,e);
                if(!std::isfinite(step)) continue;
                double next=from+step;
                auto old=distance.find(v);
                if(old!=distance.end() && old->second<=next) continue;
                distance[v]=next;back.erase(v);back.emplace(v,e);queue.emplace(next+remaining(v),-next,v);
            }
        }
        if(restart) continue;
        if(!reached) return false;
    }
    try {graph.validate(net,tree);} catch(const std::invalid_argument&) {return false;}
    return true;
}
Routes SparseMaze::seed(int stride,bool allowDieDetour) const {
    Measured measured(Timing::Maze);
    if(stride<1) throw std::invalid_argument("Sparse pitch must be positive");
    const auto& pins=graph.terminals(net);
    if(pins.empty()) throw std::invalid_argument("Empty net");
    while(true) {
        for(auto source:pins.front()) {
            Route tree{source,{}};
            if(grow(tree,stride,allowDieDetour))
                return {std::move(tree)};
        }
        if(stride==1) return {};
        Profile::retry();
        stride=std::max(1,stride/2);
    }
}
Routes SparseMaze::candidates(int stride,bool allowDieDetour) const {
    const auto found=seed(stride,allowDieDetour);
    if(found.empty()) return {};
    return PatternSearch(graph,net,time).refineMaze(found.front());
}
}
