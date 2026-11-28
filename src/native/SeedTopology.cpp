#include "SeedTopology.h"
#include "MedianRegion.h"
#include "Profile.h"
#include <numeric>

namespace native3d {
namespace {
struct Components {
    std::vector<int> parent;
    explicit Components(int n):parent(n) {std::iota(parent.begin(),parent.end(),0);}
    int find(int i) {while(i!=parent[i]) {parent[i]=parent[parent[i]];i=parent[i];}return i;}
    void merge(int u,int v) {parent[find(u)]=find(v);}
};
}
Routes initialCandidates(const RoutingGraph& graph,int net,const Deadline& time) {
    const auto& pins=graph.terminals(net);
    const int count=pins.size();
    if(!count) return {};
    PatternSearch pattern(graph,net,time);
    if(graph.model.nets[net].localNet[0]<0 || graph.model.nets[net].localNet[1]<0)
        return pattern.localSeed();
    Measured measured(Timing::CrossDieSeed);
    std::vector<XY> xy;
    for(const auto& p:pins) xy.push_back(graph.center(p.front()));
    Routes candidates;
    auto realize=[&](Components parts,const std::vector<std::tuple<int,int,Edge>>& crossings) {
        std::map<int,std::vector<std::vector<GRPoint>>> local;
        for(int i=0;i<count;++i) local[parts.find(i)].push_back(pins[i]);
        Route route{pins.front().front(),{}};
        for(const auto& cross:crossings) {
            const auto& h=std::get<2>(cross);
            local[parts.find(std::get<0>(cross))].push_back({h.a});
            local[parts.find(std::get<1>(cross))].push_back({h.b});
            route.edges.insert(h);
        }
        for(const auto& component:local) {
            auto tree=pattern.localSeed(component.second);
            if(tree.empty()) return;
            if(component.first==parts.find(0)) route.root=tree.front().root;
            route.edges.insert(tree.front().edges.begin(),tree.front().edges.end());
        }
        try {graph.validate(net,route);} catch(const std::invalid_argument&) {return;}
        candidates.push_back(std::move(route));
    };
    // 单 HBT 的可行初解，凸区域由所有真实端点的逐轴中位数给出。
    Components single(count);
    int first[2]={-1,-1};
    for(int i=0;i<count;++i) {
        int d=pins[i].front().dieIdx;
        if(first[d]<0) first[d]=i;else single.merge(first[d],i);
    }
    if(first[0]<0 || first[1]<0) return pattern.localSeed();
    std::vector<unsigned> weights;
    for(const auto& p:pins) weights.push_back(graph.l1Weight(p.front().dieIdx));
    auto region=weightedMedianRegion(xy,weights);
    auto plateauSite=[&](const std::set<int>& excluded) {
        const auto& cfg=graph.model.config;
        if(!cfg.value("enable_progressive_hbt_search",false)) return graph.seedLanding(net,region.high,{},excluded);
        const int initial=integer(cfg.value("hbt_candidate_limit",Json(8)));
        if(initial<1 || initial>128) throw std::invalid_argument("Invalid progressive HBT search width");
        std::set<int> seen=excluded;
        for(size_t k=initial;;k*=2) {
            const auto near=graph.sitesNearRegion(region.low,region.high,k);
            std::vector<Edge> selected;
            auto best=std::make_pair(std::numeric_limits<double>::infinity(),-1);
            for(const auto* site:near) {
                time.check();
                if(!seen.insert(site->id).second) continue;
                Profile::count(Event::HbtSites);
                double proxy=0;
                for(size_t i=0;i<xy.size();++i) for(int dim:{0,1})
                    proxy+=weights[i]*std::abs(double(xy[i][dim])-site->bonded[dim])/graph.model.units;
                for(const auto& e:site->transitions) if(graph.available(net,{e})) {
                    const auto rank=std::make_pair(proxy+graph.landingRisk(e),site->id);
                    if(rank<best) {best=rank;selected={e};}
                }
            }
            if(!selected.empty() || near.size()<k) return selected;
        }
    };
    auto h=plateauSite({});
    if(h.empty()) return {};
    realize(single,{{first[0],first[1],h.front()}});
    // 在含 Die/层切换下界的端点度量上生成 Prim 树。每条跨 Die 分支有独立 landing；
    // 最后按实际的局部模式路径、共享资源和物理站点重新评价。
    if(count>2) {
        Components parts(count);
        std::vector<double> dist(count,std::numeric_limits<double>::infinity());
        std::vector<int> parent(count,-1);
        std::vector<bool> done(count,false);
        const int threshold=integer(graph.model.config.value("prim_min_pins",Json(128)));
        if(threshold<3) throw std::invalid_argument("prim_min_pins must be at least three");
        const bool blocked=graph.model.config.value("enable_blocked_prim",false) && count>=threshold;
        struct Block {
            std::vector<int> ids;
            XY lo,hi;
            int layer;
            double maximum=std::numeric_limits<double>::infinity();
            std::pair<double,int> minimum{std::numeric_limits<double>::infinity(),-1};
        };
        std::vector<Block> blocks;
        std::vector<size_t> blockOf;
        std::vector<int> states;
        if(blocked) {
            std::vector<int> order(count);
            std::iota(order.begin(),order.end(),0);blockOf.resize(count);
            for(const auto& pin:pins) states.push_back(graph.stateIndex(pin.front()));
            std::sort(order.begin(),order.end(),[&](int a,int b){return std::tie(states[a],xy[a],a)<std::tie(states[b],xy[b],b);});
            for(int i:order) {
                if(blocks.empty() || blocks.back().layer!=states[i] || blocks.back().ids.size()>=16)
                    blocks.push_back({{},xy[i],xy[i],states[i]});
                auto& b=blocks.back();b.ids.push_back(i);blockOf[i]=blocks.size()-1;
                if(b.minimum.second<0 || i<b.minimum.second) b.minimum.second=i;
                for(int a:{0,1}) {b.lo[a]=std::min(b.lo[a],xy[i][a]);b.hi[a]=std::max(b.hi[a],xy[i][a]);}
            }
            blocks[blockOf[0]].minimum={0.,0};
        }
        std::vector<std::tuple<int,int,Edge>> crossings;
        Edges sites;
        dist[0]=0;
        for(int k=0;k<count;++k) {
            time.check();
            int u=-1;
            if(blocked) {
                for(const auto& b:blocks) if(b.minimum.second>=0 && (u<0 || b.minimum<std::make_pair(dist[u],u))) u=b.minimum.second;
            } else for(int i=0;i<count;++i) if(!done[i] && (u<0 || dist[i]<dist[u])) u=i;
            done[u]=true;
            if(parent[u]>=0) {
                int v=parent[u];
                if(pins[v].front().dieIdx==pins[u].front().dieIdx) parts.merge(u,v);
                else {
                    XY midpoint{{(xy[u][0]+xy[v][0])/2,(xy[u][1]+xy[v][1])/2}};
                    auto site=graph.seedLanding(net,midpoint,sites);
                    if(site.empty()) return candidates;
                    sites.insert(site.front());
                    int top=pins[u].front().dieIdx?v:u,btm=top==u?v:u;
                    crossings.emplace_back(top,btm,site.front());
                }
            }
            auto relax=[&](int v) {
                auto length=std::abs(xy[u][0]-xy[v][0])+std::abs(xy[u][1]-xy[v][1]);
                double estimate=length*graph.minimumCostPerBondDbu()+
                    graph.layerDistance(graph.stateIndex(pins[u].front()),graph.stateIndex(pins[v].front()));
                if(estimate<dist[v]) {dist[v]=estimate;parent[v]=u;}
            };
            if(!blocked) {for(int v=0;v<count;++v) if(!done[v]) relax(v);}
            else for(size_t idx=0;idx<blocks.size();++idx) {
                auto& b=blocks[idx];
                int64_t distance=0;
                for(int a:{0,1}) distance+=std::max<int64_t>({0,b.lo[a]-xy[u][a],xy[u][a]-b.hi[a]});
                const double lb=distance*graph.minimumCostPerBondDbu()+graph.layerDistance(states[u],b.layer);
                // 同一层状态分块。若块的最低可能距离已经达到所有当前键的最大值，
                // 旧 Prim 的严格小于更新不能改变其中任何父边。
                if(idx!=blockOf[u] && lb>=b.maximum) continue;
                b.maximum=-1.;b.minimum={std::numeric_limits<double>::infinity(),-1};
                for(int v:b.ids) if(!done[v]) {
                    relax(v);b.maximum=std::max(b.maximum,dist[v]);
                    if(b.minimum.second<0 || std::make_pair(dist[v],v)<b.minimum) b.minimum={dist[v],v};
                }
            }
        }
        if(!crossings.empty()) realize(parts,crossings);
    }
    // 当最近站点两侧的实际金属路径失败时，给初解一段独立寻址时间。
    // 该时间仅影响上界构造；完整候选 DP 不排除任何被跳过的站点。
    if(candidates.empty()) {
        auto budget=graph.model.config.value("seed_site_seconds",0.04);
        if(!std::isfinite(budget)||budget<0) throw std::invalid_argument("Invalid seed site time");
        auto begin=std::chrono::steady_clock::now();
        std::set<int> excluded{h.front().site};
        while(std::chrono::duration<double>(std::chrono::steady_clock::now()-begin).count()<budget) {
            time.check();
            Profile::retry();
            auto next=plateauSite(excluded);
            if(next.empty()) break;
            excluded.insert(next.front().site);
            realize(single,{{first[0],first[1],next.front()}});
            if(!candidates.empty()) break;
        }
    }
    std::sort(candidates.begin(),candidates.end(),[&](const Route& a,const Route& b) {
        return graph.cost(a.edges)<graph.cost(b.edges);
    });
    return candidates;
}
}
