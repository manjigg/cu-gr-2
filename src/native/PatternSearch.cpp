#include "PatternSearch.h"
#include "BranchRepair.h"
#include "MedianRegion.h"
#include "FluteAccess.h"
#include "Profile.h"
#include "gr/PatternRoute.h"
#include <numeric>

namespace native3d {
Deadline::Deadline(double seconds) {
    if(!std::isfinite(seconds)||seconds<=0||seconds>31536000) throw std::invalid_argument("Invalid solver time limit");
    end=std::chrono::steady_clock::now()+std::chrono::milliseconds(static_cast<int64_t>(seconds*1000));
}
void Deadline::check() const { if(std::chrono::steady_clock::now()>=end) throw SearchTimeout(); }
static void vias(Edges& edges,GRPoint& p,int to) {
    while(p.layerIdx!=to) {
        GRPoint q=p; q.layerIdx+=p.layerIdx<to?1:-1;
        edges.emplace(p,q,GREdgeType::VIA); p=q;
    }
}
static Edges join(const Edges& a,const Edges& b) {
    Edges out=a;out.insert(b.begin(),b.end());return out;
}
std::set<Edges> PatternSearch::localPaths(GRPoint a,GRPoint b) const {
    Profile::count(Event::LocalPaths);
    time.check();
    std::set<Edges> results;
    if(a.dieIdx!=b.dieIdx) return results;
    const auto& grid=g.model.dies[a.dieIdx]->grid;
    // 只为非零线段选层。两种弯折顺序共用相邻层 Via 栈构造。
    for(int first:{0,1}) {
        std::function<void(GRPoint,int,const Edges&)> segment;
        segment=[&](GRPoint p,int k,const Edges& edges) {
            if(k==2) {
                auto complete=edges; vias(complete,p,b.layerIdx);
                if(g.available(net,complete)) results.insert(std::move(complete));
                return;
            }
            int axis=(first+k)%2;
            if(p[axis]==b[axis]) {segment(p,k+1,edges);return;}
            for(int l=1;l<int(grid.getNumLayers());++l) if(int(grid.getLayerDirection(l))==axis) {
                time.check();
                auto line=edges; auto cur=p; vias(line,cur,l);
                while(cur[axis]!=b[axis]) {
                    auto q=cur;q[axis]+=b[axis]>cur[axis]?1:-1;
                    line.emplace(cur,q,GREdgeType::WIRE);cur=q;
                }
                segment(cur,k+1,line);
            }
        };
        segment(a,0,{});
    }
    return results;
}
Routes PatternSearch::localSeed() const {
    auto r=localSeed(g.terminals(net));
    if(!r.empty()) {
        try {g.validate(net,r.front());} catch(const std::invalid_argument&) {return {};}
    }
    return r;
}
Routes PatternSearch::localSeed(const std::vector<std::vector<GRPoint>>& pins) const {
    Measured measured(Timing::LocalSeed);
    if(pins.empty()) return {};
    const int die=pins.front().front().dieIdx;
    for(const auto& pin:pins) if(pin.front().dieIdx!=die) return {};
    const auto& grid=g.model.dies[die]->grid;
    GRNet local(net,"incumbent",pins,nullptr);
    PatternRoute pattern(local,grid,g.parameters());
    pattern.constructSteinerTree();pattern.constructRoutingDAG();pattern.run();
    const auto& tree=local.getRoutingTree();
    if(!tree) return {};
    Route r{GRPoint(die,tree->layerIdx,tree->x,tree->y),{}};
    GRTreeNode::preorder(tree,[&](std::shared_ptr<GRTreeNode> p) {
        for(const auto& q:p->children) {
            GRPoint cur(die,p->layerIdx,p->x,p->y);
            if(q->incoming==GREdgeType::VIA) vias(r.edges,cur,q->layerIdx);
            else {
                const int dir=grid.getLayerDirection(cur.layerIdx);
                while(cur[dir]!=(*q)[dir]) {
                    auto next=cur; next[dir]+=(*q)[dir]>cur[dir]?1:-1;
                    r.edges.emplace(cur,next,GREdgeType::WIRE);cur=next;
                }
            }
        }
    });
    // 去除二维初解为接入预留的悬空线段，保留所有真实 Pin 的接入点。
    std::map<GRPoint,std::set<Edge>> adj;
    adj[r.root];
    for(const auto& e:r.edges){adj[e.a].insert(e);adj[e.b].insert(e);}
    std::set<GRPoint> terminals;
    for(const auto& pin:pins) {
        auto hit=std::find_if(pin.begin(),pin.end(),[&](GRPoint p){return adj.count(p);});
        if(hit==pin.end()) return {};
        terminals.insert(*hit);
    }
    std::vector<GRPoint> leaves;
    for(const auto& item:adj) if(item.second.size()==1) leaves.push_back(item.first);
    while(!leaves.empty()) {
        auto p=leaves.back();leaves.pop_back();
        if(terminals.count(p)||adj[p].size()!=1) continue;
        auto e=*adj[p].begin(); auto q=p==e.a?e.b:e.a;
        r.edges.erase(e);adj[p].clear();adj[q].erase(e);
        if(adj[q].size()==1) leaves.push_back(q);
    }
    r.root=*terminals.begin();
    if(g.available(net,r.edges) && r.edges.size()+1==std::count_if(adj.begin(),adj.end(),
       [&](const decltype(adj)::value_type& v){return !v.second.empty() || v.first==r.root;})) return {std::move(r)};
    if(pins.size()!=2) return repairLocalBranches(g,net,r,terminals,time);
    Routes alternative;
    double best=std::numeric_limits<double>::infinity();
    for(auto a:pins[0]) for(auto b:pins[1]) for(const auto& edges:localPaths(a,b)) {
        double cost=g.cost(edges);
        if(cost<best) {best=cost;alternative={{a,edges}};}
    }
    return alternative;
}
std::set<Edges> PatternSearch::paths(GRPoint a,GRPoint b,bool detours,PathCache* cache,bool fast) const {
    auto pathsFor=[&](GRPoint u,GRPoint v)->std::shared_ptr<const PathList> {
        const auto key=std::make_pair(u,v);
        if(cache) {
            const auto found=cache->find(key);
            if(found!=cache->end()) return found->second;
        }
        auto generated=std::make_shared<const PathList>(localPaths(u,v));
        if(cache && cache->size()<512) cache->emplace(key,generated);
        return generated;
    };
    std::set<Edges> out;
    if(a.dieIdx==b.dieIdx) out=*pathsFor(a,b);
    if(detours && a.dieIdx==b.dieIdx && (a.x!=b.x || a.y!=b.y)) {
        const auto& grid=g.model.dies[a.dieIdx]->grid;
        GRNet local(net,"detour",{{a},{b}},nullptr);
        auto tree=std::make_shared<SteinerTreeNode>(a,utils::IntervalT<int>(a.layerIdx));
        tree->children.push_back(std::make_shared<SteinerTreeNode>(b,utils::IntervalT<int>(b.layerIdx)));
        PatternRoute original(local,grid,g.parameters());
        original.setSteinerTree(tree);
        original.constructRoutingDAG();
        original.constructDetours(g.congestion().at(a.dieIdx));
        original.run();
        Edges extra;
        GRTreeNode::preorder(local.getRoutingTree(),[&](std::shared_ptr<GRTreeNode> p) {
            for(const auto& q:p->children) {
                GRPoint cur(a.dieIdx,p->layerIdx,p->x,p->y);
                if(q->incoming==GREdgeType::VIA) vias(extra,cur,q->layerIdx);
                else {
                    int dir=grid.getLayerDirection(cur.layerIdx);
                    while(cur[dir]!=(*q)[dir]) {
                        auto next=cur; next[dir]+=(*q)[dir]>cur[dir]?1:-1;
                        extra.emplace(cur,next,GREdgeType::WIRE);cur=next;
                    }
                }
            }
        });
        if(g.available(net,extra)) out.insert(std::move(extra));
    }
    // L1 凸函数的子水平集排序。保留全部合法离散站点，精确搜索不截断排名。
    XY pa=g.center(a),pb=g.center(b);
    auto distance=[&](const Site* s) {
        return double(g.l1Weight(a.dieIdx))*(std::abs(s->bonded[0]-pa[0])+std::abs(s->bonded[1]-pa[1]))+
               double(g.l1Weight(b.dieIdx))*(std::abs(s->bonded[0]-pb[0])+std::abs(s->bonded[1]-pb[1]));
    };
    if(a.dieIdx==b.dieIdx && !detours) return out;
    Measured measured(Timing::HbtSearch);
    const bool bounded=fast && g.model.config.value("enable_hbt_index",true);
    if(bounded) {
        const auto raw=g.model.config.value("hbt_candidate_limit",8);
        if(raw<1 || raw>128) throw std::invalid_argument("Invalid HBT search limit");
        const bool cross=a.dieIdx!=b.dieIdx;
        // 同 Die 短分支已有可行的局部模式时，按需在拥塞绕行中尝试额外的双 HBT。
        if(!cross && !out.empty()) {
            bool pressured=false;
            const auto& grid=g.model.dies[a.dieIdx]->grid;
            const auto& congestion=g.congestion()[a.dieIdx];
            if(!congestion.empty()) for(const auto& e:*out.begin()) if(e.type==GREdgeType::WIRE)
                pressured|=congestion[grid.getLayerDirection(e.a.layerIdx)][e.a.x][e.a.y];
            if(!pressured) return out;
        }
        const auto region=weightedMedianRegion({pa,pb},{g.l1Weight(a.dieIdx),g.l1Weight(b.dieIdx)});
        std::set<std::pair<int,int>> seen;
        double best=std::numeric_limits<double>::infinity();
        for(size_t limit=raw;limit<=std::max<size_t>(32,raw);limit*=2) {
            auto starts=cross?g.sitesNearRegion(region.low,region.high,limit):g.sitesNearRegion(pa,pa,limit);
            auto finishes=cross?std::vector<const Site*>{}:g.sitesNearRegion(pb,pb,limit);
            auto score=[&](const Site* site) {
                double lower=std::numeric_limits<double>::infinity();
                for(const auto& h:site->transitions)
                    lower=std::min(lower,cross?g.singleHbtLowerBound(a,b,h):distance(site));
                return std::make_pair(lower,site->id);
            };
            std::sort(starts.begin(),starts.end(),[&](const Site* x,const Site* y){return score(x)<score(y);});
            for(const auto* site:starts) {
                Profile::count(Event::HbtSites);
                auto connect=[&](const Edge& h,const Edge* second) {
                    time.check();
                    if(!g.available(net,{h})) return;
                    auto u=a.dieIdx?h.b:h.a;
                    auto v=a.dieIdx?h.a:h.b;
                    Edges crossing{h};
                    if(second) crossing.insert(*second);
                    auto last=second?(a.dieIdx?second->b:second->a):v;
                    if(second && !g.available(net,crossing)) return;
                    auto ahead=pathsFor(a,u);
                    if(ahead->empty()) return;
                    auto tail=pathsFor(last,b);
                    if(tail->empty()) return;
                    auto middle=second?pathsFor(v,a.dieIdx?second->a:second->b):
                                       std::make_shared<const PathList>(PathList{Edges{}});
                    for(const auto& left:*ahead) for(const auto& right:*tail) for(const auto& mid:*middle) {
                        time.check();
                        auto whole=join(join(left,right),join(mid,crossing));
                        if(!g.available(net,whole)) continue;
                        best=std::min(best,g.cost(whole));
                        out.insert(std::move(whole));
                    }
                };
                if(cross) {
                    if(!seen.emplace(site->id,-1).second) continue;
                    // 本地上界可用于单条独立分支，联合 DP 仍保留多个替代站。
                    if(g.model.config.value("enable_hbt_pruning",true) && out.size()>=static_cast<size_t>(raw) &&
                       score(site).first>best+8.) continue;
                    for(const auto& h:site->transitions) connect(h,nullptr);
                } else for(const auto* end:finishes) {
                    if(end==site || !seen.emplace(site->id,end->id).second) continue;
                    Profile::count(Event::HbtSites);
                    for(const auto& h:site->transitions) for(const auto& second:end->transitions) connect(h,&second);
                }
            }
            if(std::isfinite(best) || starts.size()<limit) break;
        }
        return out;
    }
    std::vector<const Site*> sites;
    for(const auto& item:g.sites) if(!item.second.transitions.empty()) sites.push_back(&item.second);
    std::stable_sort(sites.begin(),sites.end(),[&](const Site* x,const Site* y){return distance(x)<distance(y);});
    for(auto site:sites) for(const auto& h:site->transitions) {
        time.check();
        Profile::count(Event::HbtSites);
        GRPoint u=a.dieIdx?h.b:h.a, v=a.dieIdx?h.a:h.b;
        if(!g.available(net,{h})) continue;
        auto starts=pathsFor(a,u);
        auto splice=[&](const Edges& middle,GRPoint last) {
            const auto tails=pathsFor(last,b);
            for(const auto& left:*starts) for(const auto& right:*tails) {
                time.check();
                auto whole=join(join(left,middle),right);
                if(g.available(net,whole)) out.insert(std::move(whole));
            }
        };
        if(a.dieIdx!=b.dieIdx) splice({h},v);
        else for(auto other:sites) if(other!=site) for(const auto& t:other->transitions) {
            time.check();
            Profile::count(Event::HbtSites);
            GRPoint w=a.dieIdx?t.a:t.b, z=a.dieIdx?t.b:t.a;
            const auto mids=pathsFor(v,w);
            for(auto mid:*mids) {
                mid.insert(h);mid.insert(t);splice(mid,z);
            }
        }
    }
    return out;
}
struct ProjectedTopology {
    std::map<XY,std::vector<int>> projection;
    std::map<XY,std::set<XY>> adjacent;
};
static std::shared_ptr<const ProjectedTopology> projectTopology(const RoutingGraph& g,int net) {
    const auto& pins=g.terminals(net);
    if(pins.empty()) throw std::invalid_argument("Cannot route an empty physical terminal set");
    auto output=std::make_shared<ProjectedTopology>();
    auto& projection=output->projection;
    for(int i=0;i<int(pins.size());++i) projection[g.center(pins[i].front())].push_back(i);
    std::vector<int> xs,ys;
    for(const auto& item:projection) {
        for(auto coord:item.first) if(coord<INT_MIN || coord>INT_MAX) throw std::out_of_range("FLUTE coordinate");
        xs.push_back(item.first[0]);ys.push_back(item.first[1]);
    }
    auto& adj=output->adjacent;
    const int degree=xs.size();
    if(degree>1) {
        auto range=[](const std::vector<int>& v){auto p=std::minmax_element(v.begin(),v.end());return int64_t(*p.second)-*p.first;};
        if((range(xs)+range(ys))*degree>INT_MAX) throw std::out_of_range("FLUTE length bound exceeds DTYPE");
        Tree t;
        {
            Measured fluteTime(Timing::Flute);
            std::lock_guard<std::mutex> access(fluteMutex());
            t=flute(degree,xs.data(),ys.data(),ACCURACY);
        }
        std::unique_ptr<Branch,decltype(&free)> memory(t.branch,free);
        for(int i=0;i<2*degree-2;++i) {
            auto u=t.branch[i],v=t.branch[u.n];
            XY x{{u.x,u.y}},y{{v.x,v.y}};
            if(x!=y) {adj[x].insert(y);adj[y].insert(x);}
        }
    }
    return output;
}
Routes PatternSearch::enumerate(bool detours) const {
    Measured measured(Timing::PatternDP);
    const auto& pins=g.terminals(net);
    const auto topology=projectTopology(g,net);
    const auto& projection=topology->projection;
    const auto& adj=topology->adjacent;
    // 每个子树的标签由根的真实物理状态与完整边集合索引。
    // 只有两者完全相同才合并；未来的共享、冲突和回溯完全相同。
    using Labels=std::map<GRPoint,std::set<Edges>>;
    std::set<XY> visited;
    std::function<Labels(XY)> solve=[&](XY xy) {
        time.check();visited.insert(xy);
        Labels states;
        for(int d=0;d<2;++d) {
            if(g.model.nets[net].localNet[d]<0 && !detours) continue;
            const auto& die=*g.model.dies[d];
            // 连续骨架点可落在 DBU 之间。按 bonded 格边做比较，用格中心表示逻辑节点。
            int idx[2];bool inside=true;
            for(int axis=0;axis<2;++axis) {
                int localAxis=die.transform.axes[axis];
                int64_t value=(xy[axis]-die.transform.offset[axis])*die.transform.signs[axis];
                const auto& lines=die.design->getGridlines()[localAxis];
                if(value<lines.front()*die.transform.scale || value>lines.back()*die.transform.scale) {inside=false;break;}
                idx[localAxis]=g.cell(d,localAxis,static_cast<int64_t>(std::floor(double(value)/die.transform.scale)));
            }
            if(inside) for(int l=0;l<int(die.grid.getNumLayers());++l)
                states[GRPoint(d,l,idx[0],idx[1])].emplace();
        }
        auto merge=[&](const Labels& child) {
            for(auto& state:states) {
                std::set<Edges> next;
                for(const auto& target:child) {
                    if(state.second.empty() || target.second.empty()) continue;
                    auto links=paths(state.first,target.first,detours);
                    for(const auto& a:state.second) for(const auto& b:target.second) for(const auto& path:links) {
                        time.check();
                        Profile::count(Event::LabelMerges);
                        auto combined=join(join(a,b),path);
                        if(g.available(net,combined) && next.insert(std::move(combined)).second) {
                            Profile::count(Event::LabelsGenerated);
                            Profile::labels(next.size());
                        } else Profile::count(Event::LabelsPruned);
                    }
                }
                state.second.swap(next);
            }
        };
        auto at=projection.find(xy);
        if(at!=projection.end()) for(int pin:at->second) {
            Labels leaf;
            for(auto ap:pins[pin]) leaf[ap].emplace();
            merge(leaf);
        }
        const auto children=adj.find(xy);
        if(children!=adj.end()) for(auto next:children->second) if(!visited.count(next)) merge(solve(next));
        return states;
    };
    Routes result;
    std::set<Edges> seen;
    for(auto& state:solve(projection.begin()->first)) for(const auto& edges:state.second) {
        Route candidate{state.first,edges};
        try {g.validate(net,candidate);} catch(const std::invalid_argument&) {continue;}
        if(seen.insert(edges).second) result.push_back(std::move(candidate));
    }
    return result;
}
Routes PatternSearch::enumerateFast(bool detours,FastPatternCache& cache) const {
    Measured measured(Timing::PatternDP);
    const auto& pins=g.terminals(net);
    const auto& cfg=g.model.config;
    const auto requested=cfg.value("beam_width",8);
    if(requested<1 || requested>128) throw std::invalid_argument("Invalid beam width");
    const size_t width=pins.size()>32?std::max(2,requested/2):requested;
    PathCache pathCache;
    auto* memo=cfg.value("enable_path_cache",true)?&pathCache:nullptr;
    if(pins.size()==2) {
        // 两 Pin 树没有内部 Steiner 子问题，直接枚举两端实际接入点及合法跨接。
        Routes found;
        std::set<Edges> seen;
        try {
            for(auto a:pins[0]) for(auto b:pins[1]) {
                for(const auto& edges:paths(a,b,detours,memo,true)) {
                    time.check();
                    Route route{a,edges};
                    try {g.validate(net,route);} catch(const std::invalid_argument&) {continue;}
                    if(seen.insert(edges).second) found.push_back(std::move(route));
                }
            }
        } catch(const SearchTimeout&) {
            if(found.empty()) throw;
            Profile::retry();
        }
        return found;
    }
    if(!cache.topology || !cfg.value("enable_topology_cache",true)) cache.topology=projectTopology(g,net);
    const auto& topology=*cache.topology;
    struct Label {
        // 已排序的单位边。合并使用线性并集，复用不可变标签，完整根才重建 Route。
        std::vector<Edge> edges;
        PathQuality quality;
    };
    using Bucket=std::vector<Label>;
    using Table=std::map<GRPoint,Bucket>;
    auto keepDiverse=[&](Bucket& labels) {
        if(labels.size()<=width) return;
        auto less=[](const Label& a,const Label& b) {
            return std::tie(a.quality.cost,a.quality.wire,a.quality.vias,a.quality.hbt,a.edges)<
                   std::tie(b.quality.cost,b.quality.wire,b.quality.vias,b.quality.hbt,b.edges);
        };
        std::sort(labels.begin(),labels.end(),less);
        const auto count=labels.size();
        std::vector<bool> selected(count,false);
        Bucket kept;
        auto take=[&](size_t index) {
            if(!selected[index] && kept.size()<width) {
                selected[index]=true;
                kept.push_back(std::move(labels[index]));
            }
        };
        auto best=[&](const auto& rank) {
            auto index=count;
            for(size_t i=0;i<count;++i)
                if(!selected[i] && (index==count || rank(labels[i])<rank(labels[index]))) index=i;
            if(index<count) take(index);
        };
        take(0);
        best([](const Label& label){return label.quality.hbt.size();});
        best([](const Label& label){return label.quality.wire;});
        best([](const Label& label){return label.quality.vias;});
        std::set<std::vector<int>> sites;
        for(const auto& label:kept) sites.insert(label.quality.hbt);
        for(size_t i=0;i<count && kept.size()<width;++i)
            if(!selected[i] && sites.insert(labels[i].quality.hbt).second) take(i);
        for(size_t i=0;i<count && kept.size()<width;++i) take(i);
        Profile::count(Event::LabelsPruned,count-kept.size());
        labels.swap(kept);
    };
    auto merge=[&](const Table& a,const Table& b) {
        Table result;
        for(const auto& from:a) {
            Bucket next;
            for(const auto& to:b) {
                for(const auto& path:paths(from.first,to.first,detours,memo,true)) {
                    for(const auto& left:from.second) for(const auto& right:to.second) {
                        time.check();
                        Profile::count(Event::LabelMerges);
                        std::vector<Edge> child;
                        child.reserve(left.edges.size()+right.edges.size());
                        std::set_union(left.edges.begin(),left.edges.end(),right.edges.begin(),right.edges.end(),
                                       std::back_inserter(child));
                        Label combined;
                        combined.edges.reserve(child.size()+path.size());
                        std::set_union(child.begin(),child.end(),path.begin(),path.end(),std::back_inserter(combined.edges));
                        auto duplicate=std::any_of(next.begin(),next.end(),[&](const Label& old) {
                            return !(old.edges<combined.edges) && !(combined.edges<old.edges);
                        });
                        if(duplicate || !g.evaluateCompact(net,combined.edges,combined.quality)) {
                            Profile::count(Event::LabelsPruned);
                            continue;
                        }
                        next.push_back(std::move(combined));
                        Profile::count(Event::LabelsGenerated);
                        Profile::labels(next.size());
                        if(next.size()>=2*width) keepDiverse(next);
                    }
                }
            }
            keepDiverse(next);
            if(!next.empty()) result.emplace(from.first,std::move(next));
        }
        return result;
    };
    std::set<XY> visited;
    std::function<Table(XY)> solve=[&](XY xy) {
        time.check();visited.insert(xy);
        Table table;
        for(int d:{0,1}) {
            if(!detours && g.model.nets[net].localNet[d]<0) continue;
            const auto& die=*g.model.dies[d];
            std::array<int,2> cell{};
            bool inside=true;
            for(int i:{0,1}) {
                const int axis=die.transform.axes[i];
                const auto v=(xy[i]-die.transform.offset[i])*die.transform.signs[i];
                const auto& lines=die.design->getGridlines()[axis];
                if(v<lines.front()*die.transform.scale || v>lines.back()*die.transform.scale) {inside=false;break;}
                cell[axis]=g.cell(d,axis,static_cast<int64_t>(std::floor(double(v)/die.transform.scale)));
            }
            if(inside) for(int l=0;l<int(die.grid.getNumLayers());++l)
                table[GRPoint(d,l,cell[0],cell[1])].emplace_back();
        }
        const auto point=topology.projection.find(xy);
        if(point!=topology.projection.end()) for(int pin:point->second) {
            Table terminal;
            for(auto ap:pins[pin]) terminal[ap].emplace_back();
            table=merge(table,terminal);
        }
        const auto neighbors=topology.adjacent.find(xy);
        if(neighbors!=topology.adjacent.end()) for(auto child:neighbors->second)
            if(!visited.count(child)) table=merge(table,solve(child));
        return table;
    };
    Routes routes;
    std::set<Edges> seen;
    for(const auto& state:solve(topology.projection.begin()->first)) for(const auto& label:state.second) {
        Route route{state.first,Edges(label.edges.begin(),label.edges.end())};
        try {g.validate(net,route);} catch(const std::invalid_argument&) {continue;}
        if(seen.insert(route.edges).second) routes.push_back(std::move(route));
    }
    return routes;
}

// 将迷宫树中的无分支同层线段送回模式路径 DP；物理 HBT 和真实端点作为锚点。
Routes PatternSearch::refineMaze(const Route& tree) const {
    Measured measured(Timing::PatternDP);
    std::map<GRPoint,std::vector<Edge>> adj;
    std::set<GRPoint> anchors{tree.root};
    Edges fixed;
    for(const auto& pin:g.terminals(net)) anchors.insert(pin.begin(),pin.end());
    for(const auto& e:tree.edges) {
        adj[e.a].push_back(e); adj[e.b].push_back(e);
        if(e.type!=GREdgeType::WIRE) {fixed.insert(e);anchors.insert(e.a);anchors.insert(e.b);}
    }
    for(const auto& a:adj) if(a.second.size()!=2) anchors.insert(a.first);
    std::set<Edges> labels{fixed};
    Edges visited;
    for(auto start:anchors) for(const auto& first:adj[start]) {
        if(first.type!=GREdgeType::WIRE || !visited.insert(first).second) continue;
        GRPoint end=(start==first.a?first.b:first.a);
        while(!anchors.count(end)) {
            const auto& options=adj.at(end);
            const Edge& e=visited.count(options.front())?options.back():options.front();
            visited.insert(e);end=end==e.a?e.b:e.a;
        }
        const auto paths=localPaths(start,end);
        std::set<Edges> next;
        for(const auto& a:labels) for(const auto& p:paths) {
            time.check(); auto u=join(a,p);
            if(g.available(net,u)) next.insert(std::move(u));
        }
        labels.swap(next);
    }
    Routes result{tree};
    for(const auto& e:labels) {
        if(!(e<tree.edges) && !(tree.edges<e)) continue;
        Route r{tree.root,e};
        try {g.validate(net,r);} catch(const std::invalid_argument&) {continue;}
        result.push_back(std::move(r));
    }
    return result;
}
MasterResult selectRoutes(RoutingGraph& g,const std::vector<Routes>& all,const Deadline& time,const std::vector<unsigned>& feasible) {
    Measured measured(Timing::Master);
    MasterResult result;
    const int count=all.size();
    std::vector<std::vector<std::pair<double,unsigned>>> sorted(count);
    // 在已经连通且包含全部终端、固定站点的根标签上执行严格支配剪枝。
    // A 的物理边是真子集时，对每项共享资源的非负需求都不大于 B；
    // 任意其他网替换 B 为 A 都保持合法，所有非负价格的目标均不增。
    // 中间标签可能共享未来路径，因此仍只做边集合完全相等的合并。
    std::vector<double> suffix(count+1,0);
    bool individuallyBest=!feasible.empty();
    if(individuallyBest && feasible.size()!=all.size()) throw std::invalid_argument("Invalid incumbent certificate");
    for(int i=count-1;i>=0;--i) {
        if(all[i].empty()) { result.status="INFEASIBLE";return result; }
        std::vector<unsigned> order(all[i].size());
        std::iota(order.begin(),order.end(),0);
        std::stable_sort(order.begin(),order.end(),[&](unsigned a,unsigned b) {
            return all[i][a].edges.size()<all[i][b].edges.size();
        });
        for(unsigned k:order) {
            const auto& candidate=all[i][k].edges;
            bool dominated=false;
            for(const auto& other:sorted[i]) {
                const auto& smaller=all[i][other.second].edges;
                if(smaller.size()==candidate.size()) break;
                if(std::includes(candidate.begin(),candidate.end(),smaller.begin(),smaller.end())) {
                    dominated=true;break;
                }
            }
            if(!dominated) sorted[i].emplace_back(g.cost(candidate),k);
            else Profile::count(Event::LabelsPruned);
        }
        std::sort(sorted[i].begin(),sorted[i].end());
        // 向下舍入候选池的独立最小值之和，避免显示一个高于上界的下界。
        suffix[i]=std::max(0.,std::nextafter(suffix[i+1]+sorted[i].front().first,0.));
        if(!feasible.empty() && g.cost(all[i].at(feasible[i]).edges)!=sorted[i].front().first)
            individuallyBest=false;
    }
    result.lower=suffix[0];
    if(!feasible.empty()) {
        // 调用方已提交并核验的旧解；重新验证所有权使独立调用也有相同保证。
        int n=0;
        try { for(;n<count;++n) g.commit(n,all[n].at(feasible[n])); }
        catch(...) {while(n) g.release(--n);throw;}
        while(n) g.release(--n);
        result.objective=0.;result.selection=feasible;
        for(int i=0;i<count;++i) result.objective+=g.cost(all[i].at(feasible[i]).edges);
        if(individuallyBest) {
            result.lower=result.objective; result.status="OPTIMAL";return result;
        }
    }
    // 显式栈避免多万网设计在精确分支定界时耗尽 C++ 调用栈。
    std::vector<unsigned> selected(count),nextOption(count+1);
    std::vector<double> prefix(count+1,0);
    int depth=0;
    try {
        while(true) {
            time.check();
            if(depth==count) {
                if(prefix[depth]<result.objective) {
                    result.objective=prefix[depth];result.selection=selected;
                }
            } else if(nextOption[depth]<sorted[depth].size()) {
                Profile::count(Event::MasterNodes);
                const auto option=sorted[depth][nextOption[depth]++];
                const double newCost=prefix[depth]+option.first;
                // 向外放宽浮点下界，避免舍入误差导致错误剪枝。
                double scale=std::max(1.,std::abs(result.objective));
                if(newCost+suffix[depth+1] <= result.objective+
                    std::numeric_limits<double>::epsilon()*scale*(count+2)) {
                    const auto& route=all[depth][option.second];
                    if(!g.available(depth,route.edges)) continue;
                    g.commit(depth,route);
                    selected[depth]=option.second;
                    ++depth;prefix[depth]=newCost;nextOption[depth]=0;
                    continue;
                }
            }
            if(depth==0) break;
            g.release(--depth);
        }
    } catch(const SearchTimeout&) {
        while(depth>0) g.release(--depth);
        result.status="TIME_LIMIT";return result;
    } catch(...) {
        while(depth>0) g.release(--depth);
        throw;
    }
    result.status=std::isfinite(result.objective)?"OPTIMAL":"INFEASIBLE";
    result.lower=result.objective;
    return result;
}
}
