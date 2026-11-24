#include "SeedTopology.h"
#include "MedianRegion.h"
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
    auto h=graph.seedLanding(net,region.high,{});
    if(h.empty()) return {};
    realize(single,{{first[0],first[1],h.front()}});
    // 在含 Die/层切换下界的端点度量上生成 Prim 树。每条跨 Die 分支有独立 landing；
    // 最后按实际的局部模式路径、共享资源和物理站点重新评价。
    if(count>2) {
        Components parts(count);
        std::vector<double> dist(count,std::numeric_limits<double>::infinity());
        std::vector<int> parent(count,-1);
        std::vector<bool> done(count,false);
        std::vector<std::tuple<int,int,Edge>> crossings;
        Edges sites;
        dist[0]=0;
        for(int k=0;k<count;++k) {
            time.check();
            int u=-1;
            for(int i=0;i<count;++i) if(!done[i] && (u<0 || dist[i]<dist[u])) u=i;
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
            for(int v=0;v<count;++v) if(!done[v]) {
                auto length=std::abs(xy[u][0]-xy[v][0])+std::abs(xy[u][1]-xy[v][1]);
                double estimate=length*graph.minimumCostPerBondDbu()+
                    graph.layerDistance(graph.stateIndex(pins[u].front()),graph.stateIndex(pins[v].front()));
                if(estimate<dist[v]) {dist[v]=estimate;parent[v]=u;}
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
            auto next=graph.seedLanding(net,region.high,{},excluded);
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
