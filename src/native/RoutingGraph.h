#pragma once
#include "MultiDieDesign.h"

namespace native3d {
struct Edge {
    GRPoint a,b;
    GREdgeType type;
    int site;
    Edge(GRPoint u, GRPoint v, GREdgeType t, int id=-1): a(std::min(u,v)), b(std::max(u,v)),type(t),site(id) {}
    bool operator<(const Edge& e) const { return std::tie(a,b,type,site)<std::tie(e.a,e.b,e.type,e.site); }
};
using Edges=std::set<Edge>;
using Resource=std::tuple<GREdgeType,int,int,int,int>;
using Demand=std::map<Resource,double>;
struct Site {
    int id,capacity,owner;
    XY bonded;
    std::array<XY,2> local;
    std::vector<Edge> transitions;
};
struct Route {
    GRPoint root;
    Edges edges;
};
class RoutingGraph {
public:
    explicit RoutingGraph(const MultiDieDesign&);
    const MultiDieDesign& model;
    std::map<int,Site> sites;
    int cell(int d,int axis,int64_t local) const;
    XY center(GRPoint p) const;
    std::vector<Edge> seedLanding(int net,XY target,const Edges& chosen,const std::set<int>& excluded = {}) const;
    std::vector<Edge> neighbors(GRPoint p,int net) const;
    void validate(int net,const Route& route) const;
    bool available(int net,const Edges& edges) const;
    double cost(const Edges& edges) const;
    // 对已由 neighbors() 给出的合法单位边，直接查询余量和静态价格。
    double stepCost(int net,const Edge&) const;
    double wireUm(const Edge& edge) const;
    Demand resources(const Edges& edges) const;
    double capacity(const Resource& id) const;
    void commit(int net,const Route& route);
    void release(int net);
    const Demand& demand() const {return used;}
    const std::vector<std::vector<GRPoint>>& terminals(int net) const {return access.at(net);}
    const std::array<GridGraphView<bool>,2>& congestion() const { return congestionView; }
    void captureCongestion();
    const Parameters& parameters() const { return model.parameters; }
    bool validPoint(GRPoint p) const;
    unsigned l1Weight(int d) const {return l1Weights.at(d);}
    int stateIndex(GRPoint p) const {return p.layerIdx+(p.dieIdx?model.dies[0]->grid.getNumLayers():0);}
    double layerDistance(int from,int to) const { return layerLower[from][to]; }
    double minimumCostPerBondDbu() const {return distancePrice;}
    const std::set<int>& landingLines(int die,int axis) const {return siteLines[die][axis];}
private:
    std::map<GRPoint,std::vector<Edge>> landings;
    RTree siteIndex;
    std::set<int> siteLines[2][2];
    std::map<int,std::vector<int>> fixedByNet;
    std::array<unsigned,2> l1Weights{{1,1}};
    double distancePrice;
    std::vector<std::vector<double>> layerLower;
    std::vector<std::vector<std::vector<GRPoint>>> access;
    std::array<GridGraphView<bool>,2> congestionView;
    void publishDemand(const Demand& changed);
    Demand used;
    std::map<int,Demand> owned;
    int viaCapacity[2];
    double spacing,wireWeight,viaWeight,hbtWeight,congestionWeight,hbtPressureWeight,shortAreaWeight;
    bool allowed(int net,const Edge& e) const;
    double resourcePrice(const Resource&,double cap,double demand) const;
};
}
