#include "TopologyRefinement.h"
#include <queue>

namespace native3d {
namespace {
using Adjacency=std::map<GRPoint,std::vector<Edge>>;
using Points=std::set<GRPoint>;
struct Change {
    std::string name;
    Edges cut,forced,forbidden;
    std::set<int> bannedSites;
    Points anchors,oldJunctions;
    bool allowNewHbt=true;
};
GRPoint other(const Edge& e,GRPoint p) {return p==e.a?e.b:e.a;}
Adjacency adjacency(const Edges& edges,const Points& required) {
    Adjacency adj;
    for(const auto& p:required) adj[p];
    for(const auto& e:edges) {adj[e.a].push_back(e);adj[e.b].push_back(e);}
    return adj;
}
std::vector<Points> components(const Adjacency& adj) {
    Points seen;
    std::vector<Points> parts;
    for(const auto& entry:adj) if(seen.insert(entry.first).second) {
        Points part{entry.first};
        std::vector<GRPoint> todo{entry.first};
        while(!todo.empty()) {
            const auto u=todo.back();todo.pop_back();
            for(const auto& edge:adj.at(u)) {
                auto v=other(edge,u);
                if(seen.insert(v).second) {part.insert(v);todo.push_back(v);}
            }
        }
        parts.push_back(std::move(part));
    }
    return parts;
}
// 局部拆线后同时保留未切除的物理边、固定 HBT 和原有接入点。每次只桥接不同分量。
bool reconnect(const RoutingGraph& graph,int net,const Points& terminals,const Change& change,
               const Deadline& deadline,Route& trial,TopologyStats& stats,const ResourceReadView* view,
               const std::function<void(const Edges&)>& observe) {
    for(const auto& e:change.cut) trial.edges.erase(e);
    trial.edges.insert(change.forced.begin(),change.forced.end());
    Points required=terminals;
    required.insert(change.anchors.begin(),change.anchors.end());
    if(!graph.available(net,trial.edges,view)) {
        if(observe) observe(trial.edges);
        ++stats.capacity;
        return false;
    }
    auto groups=components(adjacency(trial.edges,required));
    XY lo=graph.center(change.cut.begin()->a),hi=lo;
    int64_t halo=0;
    auto bound=[&](GRPoint p) {
        const auto& die=*graph.model.dies[p.dieIdx];
        const auto box=die.grid.getCellBox(p);
        halo=std::max<int64_t>(halo,4*die.transform.scale*std::max(box.x.high-box.x.low,box.y.high-box.y.low));
        const auto xy=graph.center(p);
        for(int a:{0,1}) {lo[a]=std::min(lo[a],xy[a]);hi[a]=std::max(hi[a],xy[a]);}
    };
    for(const auto& e:change.cut) {bound(e.a);bound(e.b);}
    for(const auto& e:change.forced) {bound(e.a);bound(e.b);}
    auto inWindow=[&](GRPoint p) {
        const auto xy=graph.center(p);
        return xy[0]>=lo[0]-halo && xy[0]<=hi[0]+halo && xy[1]>=lo[1]-halo && xy[1]<=hi[1]+halo;
    };
    auto demand=graph.resources(trial.edges);
    std::vector<int> sites;
    for(const auto& e:trial.edges) if(e.type==GREdgeType::HBT) sites.push_back(e.site);
    std::sort(sites.begin(),sites.end());
    while(groups.size()>1) {
        // 只从最小保留分量向其他分量生长，避免大树所有点反复进入优先队列。
        auto start=std::min_element(groups.begin(),groups.end(),[](const Points& a,const Points& b){return a.size()<b.size();});
        auto growing=change.anchors.end();
        if(!change.anchors.empty()) {
            const auto current=adjacency(trial.edges,required);
            growing=std::find_if(change.anchors.begin(),change.anchors.end(),[&](GRPoint p){return current.at(p).size()<3;});
        }
        if(growing!=change.anchors.end())
            start=std::find_if(groups.begin(),groups.end(),[&](const Points& part){return part.count(*growing);});
        Points targets;
        for(auto it=groups.begin();it!=groups.end();++it) if(it!=start) targets.insert(it->begin(),it->end());
        using Key=std::pair<double,GRPoint>;
        std::priority_queue<Key,std::vector<Key>,std::greater<Key>> queue;
        std::map<GRPoint,double> dist;
        std::map<GRPoint,Edge> back;
        for(auto p:*start) if(inWindow(p) && (growing==change.anchors.end() || p==*growing)) {
            dist.emplace(p,0.);queue.emplace(0.,p);
        }
        bool joined=false;
        while(!queue.empty()) {
            deadline.check();
            auto item=queue.top();queue.pop();
            auto p=item.second;
            if(dist.at(p)!=item.first) continue;
            if(targets.count(p)) {
                Edges path;
                while(back.count(p)) {auto e=back.at(p);path.insert(e);p=other(e,p);}
                if(!graph.extendPath(net,path,trial.edges,demand,sites,view)) {
                    if(observe) {path.insert(trial.edges.begin(),trial.edges.end());observe(path);}
                    ++stats.capacity;
                    return false;
                }
                joined=true;break;
            }
            for(const auto& e:graph.neighbors(p,net)) {
                if(trial.edges.count(e) || change.forbidden.count(e) || change.bannedSites.count(e.site) ||
                   (e.type==GREdgeType::HBT && !change.allowNewHbt)) continue;
                const auto q=other(e,p);
                if(!inWindow(q) || start->count(q)) continue;
                const double step=graph.stepCost(net,e,view);
                if(!std::isfinite(step) && observe) observe({e});
                const double next=item.first+step;
                if(!std::isfinite(next) || (dist.count(q) && dist.at(q)<=next)) continue;
                dist[q]=next;back.erase(q);back.emplace(q,e);queue.emplace(next,q);
            }
        }
        if(!joined) {++stats.connectivity;return false;}
        groups=components(adjacency(trial.edges,required));
    }
    ++stats.generated;
    try {graph.validate(net,trial,view);} catch(const std::invalid_argument&) {++stats.connectivity;return false;}
    const auto adj=adjacency(trial.edges,required);
    for(const auto& e:change.forced) for(const auto p:{e.a,e.b})
        if(!terminals.count(p) && adj.at(p).size()<2) return false;
    for(const auto p:change.anchors) if(adj.at(p).size()<3) return false;
    for(const auto p:change.oldJunctions) if(adj.count(p) && adj.at(p).size()>=3) return false;
    return true;
}
bool usefulNewCrossings(const RoutingGraph& graph,const Route& old,const Route& trial,Points terminals) {
    auto tree=adjacency(trial.edges,terminals);
    for(const auto& edge:trial.edges) if(edge.type==GREdgeType::HBT && graph.sites.at(edge.site).owner>=0) {
        terminals.insert(edge.a);
        terminals.insert(edge.b);
    }
    std::map<GRPoint,size_t> degree;
    std::vector<GRPoint> leaves;
    for(const auto& entry:tree) {
        degree.emplace(entry.first,entry.second.size());
        if(entry.second.size()==1 && !terminals.count(entry.first)) leaves.push_back(entry.first);
    }
    for(size_t i=0;i<leaves.size();++i) {
        const auto p=leaves[i];
        if(!degree.at(p)) continue;
        for(const auto& edge:tree.at(p)) {
            auto q=other(edge,p);
            if(!degree.at(q)) continue;
            if(edge.type==GREdgeType::HBT && !old.edges.count(edge)) return false;
            if(--degree.at(q)==1 && !terminals.count(q)) leaves.push_back(q);
        }
        degree.at(p)=0;
    }
    return true;
}
}
Json TopologyStats::report() const {
    return {{"attempted",attempted},{"generated",generated},{"feasible",feasible},{"accepted",accepted},
        {"rejected_by_capacity_or_spacing",capacity},{"rejected_by_connectivity",connectivity},{"timed_out",timedOut},
        {"rejected_by_budget",budget},{"rejected_by_cost",cost},{"delta_wire_um",deltaWire},{"delta_via",deltaVia},
        {"delta_hbt",deltaHbt},{"delta_base_cost",deltaCost},{"seconds",seconds},
        {"static_legal",staticLegal},{"submittable",submittable},{"retained",retained},{"joint_accepted",jointAccepted}};
}
std::vector<TopologyCandidate> refineTopology(const RoutingGraph& graph,int net,const Route& old,const Deadline& deadline,
                                              std::map<std::string,TopologyStats>& stats,const ResourceReadView* view,
                                              const std::function<void(const Edges&)>& observe) {
    std::vector<TopologyCandidate> result;
    const auto& cfg=graph.model.config;
    const int limit=integer(cfg.value("topology_attempt_limit",Json(6)));
    if(limit<1 || limit>128) throw std::invalid_argument("Invalid topology attempt limit");
    const auto adj=adjacency(old.edges,{old.root});
    Points terminals;
    for(const auto& group:graph.terminals(net)) {
        auto p=std::find_if(group.begin(),group.end(),[&](GRPoint q){return adj.count(q);});
        if(p==group.end()) throw std::logic_error("Detached incumbent pin in topology refinement");
        terminals.insert(*p);
    }
    auto pins=terminals;
    pins.insert(old.root);
    std::vector<Edge> movable,metal;
    for(const auto& e:old.edges) {
        if(e.type==GREdgeType::HBT) {
            if(graph.sites.at(e.site).owner<0) movable.push_back(e);
        } else if(e.type==GREdgeType::WIRE) metal.push_back(e);
    }
    // 割去一个分支及两端至首个终端或分叉点间的短接入链。外部边界保留为重连起点。
    auto trim=[&](Change& change,const Edge& cut) {
        change.cut.insert(cut);
        for(auto p:{cut.a,cut.b}) {
            Edge prev=cut;
            for(int i=0;i<4 && !pins.count(p) && adj.at(p).size()==2;++i) {
                const auto& incident=adj.at(p);
                const Edge& next=(incident[0]<prev || prev<incident[0])?incident[0]:incident[1];
                if(next.type==GREdgeType::HBT) break;
                change.cut.insert(next);p=other(next,p);prev=next;
            }
        }
    };
    auto hbtCount=[](const Edges& edges){return std::count_if(edges.begin(),edges.end(),[](const Edge& e){return e.type==GREdgeType::HBT;});};
    const auto oldCount=hbtCount(old.edges);
    int attempts=0;
    const bool rotate=view && cfg.value("enable_topology_rotation",false);
    std::vector<std::string> opOrder;
    std::map<std::string,std::vector<Change>> planned;
    auto tryMove=[&](Change& change) {
        if(deadline.expired() || attempts>=limit) return;
        auto& s=stats[change.name];++s.attempted;++attempts;
        const auto start=std::chrono::steady_clock::now();
        try {
            auto trial=old;
            if(reconnect(graph,net,pins,change,deadline,trial,s,view,observe)) {
                const auto count=hbtCount(trial.edges);
                if((change.name!="merge" || count<oldCount) && (change.name!="augmentation" || count>oldCount) &&
                   (change.name!="relocation" || count==oldCount) &&
                   (!view || change.name!="augmentation" || usefulNewCrossings(graph,old,trial,terminals))) {
                    ++s.feasible;result.push_back({change.name,std::move(trial)});
                }
            }
        } catch(const SearchTimeout&) {++s.timedOut;}
        s.seconds+=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
    };
    auto full=[&](const std::string& operation) {
        if(attempts>=limit || deadline.expired()) return true;
        const auto pending=planned.find(operation);
        return rotate && pending!=planned.end() && pending->second.size()>=size_t(limit);
    };
    auto submit=[&](Change& change) {
        if(rotate) {
            const auto group=planned.emplace(change.name,std::vector<Change>{});
            if(group.second) opOrder.push_back(change.name);
            group.first->second.push_back(std::move(change));
        } else tryMove(change);
    };
    if(cfg.value("enable_hbt_merge",true) && oldCount>1) for(const auto& h:movable) {
        Change change{"merge"};trim(change,h);change.bannedSites.insert(h.site);change.allowNewHbt=false;
        submit(change);if(full("merge")) break;
    }
    if(cfg.value("enable_hbt_relocation",true)) for(const auto& h:movable) {
        Change change{"relocation"};trim(change,h);change.bannedSites.insert(h.site);
        submit(change);if(full("relocation")) break;
    }
    if(cfg.value("enable_hbt_augmentation",false) && cfg.value("optimization_mode",std::string("baseline"))!="baseline") {
        for(const auto& e:metal) {
            auto xy=graph.center(e.a);
            for(const auto site:graph.sitesNearRegion(xy,xy,3)) {
                if(std::any_of(old.edges.begin(),old.edges.end(),[&](const Edge& h){return h.site==site->id;})) continue;
                for(const auto& h:site->transitions) {
                    if(!graph.available(net,{h},view)) {
                        if(observe) observe({h});
                        continue;
                    }
                    Change change{"augmentation"};trim(change,e);change.forbidden.insert(e);change.forced.insert(h);
                    submit(change);break;
                }
                if(full("augmentation")) break;
            }
            if(full("augmentation")) break;
        }
    }
    if(cfg.value("enable_steiner_migration",false)) for(const auto& item:adj) {
        if(full("steiner_migration")) break;
        if(item.second.size()<3 || pins.count(item.first) || std::any_of(item.second.begin(),item.second.end(),
                [](const Edge& e){return e.type==GREdgeType::HBT;})) continue;
        for(const auto& shift:graph.neighbors(item.first,net)) {
            const auto p=other(shift,item.first);
            if(adj.count(p)) continue;
            Change change{"steiner_migration"};change.anchors.insert(p);change.oldJunctions.insert(item.first);
            // 原分叉允许作为新支路的中间点，最终不得继续充当分叉。
            for(const auto& e:item.second) trim(change,e);
            submit(change);
            if(full("steiner_migration")) break;
        }
    }
    if(cfg.value("enable_edge_reconnection",false)) for(const auto& e:metal) {
        Change change{"edge_reconnection"};trim(change,e);change.forbidden.insert(e);
        submit(change);if(full("edge_reconnection")) break;
    }
    // 轮转共用原先的总尝试次数，不为单个操作重新开启计时。
    for(size_t turn=0;rotate && turn<size_t(limit) && attempts<limit && !deadline.expired();++turn)
        for(const auto& op:opOrder) if(turn<planned.at(op).size()) tryMove(planned.at(op)[turn]);
    return result;
}
}
