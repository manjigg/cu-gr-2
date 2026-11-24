#include "Reachability.h"
#include <numeric>
namespace native3d {
Json checkStaticReachability(const RoutingGraph& g) {
    if(!g.demand().empty()) throw std::invalid_argument("Static reachability requires an empty ledger");
    int64_t offsets[3]={};
    for(int d:{0,1}) {
        auto& grid=g.model.dies[d]->grid;
        offsets[d+1]=offsets[d]+int64_t(grid.getNumLayers())*grid.getSize(0)*grid.getSize(1);
    }
    if(offsets[2]>INT_MAX) throw std::length_error("Static connectivity index exceeds 32 bits");
    std::vector<int> parent(offsets[2]),size(offsets[2],1);
    std::iota(parent.begin(),parent.end(),0);
    auto index=[&](GRPoint p) {
        const auto& grid=g.model.dies[p.dieIdx]->grid;
        return int(offsets[p.dieIdx]+(p.layerIdx*grid.getSize(0)+p.x)*grid.getSize(1)+p.y);
    };
    auto find=[&](int i) {
        while(i!=parent[i]) {parent[i]=parent[parent[i]];i=parent[i];}
        return i;
    };
    auto unite=[&](GRPoint a,GRPoint b) {
        int i=find(index(a)),j=find(index(b));
        if(i==j) return;
        if(size[i]<size[j]) std::swap(i,j);
        size[i]+=size[j];parent[j]=i;
    };
    for(int d:{0,1}) {
        const auto& grid=g.model.dies[d]->grid;
        for(int l=0;l<int(grid.getNumLayers());++l) for(int x=0;x<int(grid.getSize(0));++x)
            for(int y=0;y<int(grid.getSize(1));++y) {
                GRPoint p(d,l,x,y),q=p;
                int axis=grid.getLayerDirection(l);++q[axis];
                if(l>=g.parameters().min_routing_layer && g.validPoint(q) &&
                   grid.getEdge(l,x,y).capacity+1e-12>=1) unite(p,q);
                q=p;++q.layerIdx;
                if(g.validPoint(q) && g.available(-1,{Edge(p,q,GREdgeType::VIA)})) unite(p,q);
            }
    }
    // 所有合法 HBT 层组合均加入宽松图，忽略站点竞争与固定所有权；
    // 在此超图上已经分离的真实终端，在更严格的最终解中也不可能连通。
    for(const auto& s:g.sites) for(const auto& edge:s.second.transitions) unite(edge.a,edge.b);
    Json report;
    report["vertices"]=offsets[2];
    report["nets_tested"]=g.model.nets.size();
    report["disconnected"]=Json::array();
    for(const auto& n:g.model.nets) {
        const auto& pins=g.terminals(n.id);
        std::set<int> common;
        bool first=true;
        for(const auto& ap:pins) {
            std::set<int> comps;
            for(auto p:ap) comps.insert(find(index(p)));
            if(first) {common.swap(comps);first=false;}
            else {
                std::set<int> next;
                std::set_intersection(common.begin(),common.end(),comps.begin(),comps.end(),std::inserter(next,next.end()));
                common.swap(next);
            }
        }
        if(!common.empty()) continue;
        Json entry;entry["net_id"]=n.id;
        for(const auto& pin:n.pins) if(!pin.mappingPort) {
            const auto& design=*g.model.dies[pin.die]->design;
            Json name;
            name["die"]=pin.die;name["net"]=design.getAllNets().at(pin.net).getName();
            name["instance"]=design.instance(pin.original.instanceIndex).getName();name["pin"]=design.pinName(pin.original);
            entry["pins"].push_back(name);
        }
        for(const auto& ap:pins) {
            Json alternatives=Json::array();
            for(auto p:ap) {
                int id=find(index(p));
                alternatives.push_back({p.dieIdx,p.layerIdx,p.x,p.y,id,size[id]});
            }
            entry["access_components"].push_back(std::move(alternatives));
        }
        report["disconnected"].push_back(std::move(entry));
    }
    return report;
}
}
