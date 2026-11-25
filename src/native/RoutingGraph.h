#pragma once
#include "MultiDieDesign.h"
#include <unordered_map>

namespace native3d {
struct Edge {
    GRPoint a,b;
    GREdgeType type;
    int site;
    Edge(GRPoint u, GRPoint v, GREdgeType t, int id=-1): a(std::min(u,v)), b(std::max(u,v)),type(t),site(id) {}
    bool operator<(const Edge& e) const { return std::tie(a,b,type,site)<std::tie(e.a,e.b,e.type,e.site); }
};
using Edges=std::set<Edge>;
using Resource=uint64_t;
using Demand=std::map<Resource,double>;
using Occupancy=std::unordered_map<Resource,double>;
struct ResourceLocation {
    GREdgeType type;
    int die,layer,x,y;
};
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
struct PathQuality {
    double cost=0,wire=0;
    size_t vias=0;
    std::vector<int> hbt;
};
class RoutingGraph;
class RouteResourceSummary {
public:
    double cost() const {return baseCost;}
    double wirelength() const {return wireUm;}
    size_t vias() const {return viaCount;}
    const std::vector<int>& hbtSites() const {return sites;}
    const std::vector<std::pair<Resource,double>>& resources() const {return demand;}
private:
    friend class RoutingGraph;
    RouteResourceSummary(const RoutingGraph* source,int id):graph(source),net(id) {}
    const RoutingGraph* graph;
    int net;
    double baseCost=0,wireUm=0;
    size_t viaCount=0;
    std::vector<int> sites;
    std::vector<std::pair<Resource,double>> demand;
};
using PreparedRoute=std::shared_ptr<const RouteResourceSummary>;

class RoutingGraph {
public:
    explicit RoutingGraph(const MultiDieDesign&);
    const MultiDieDesign& model;
    std::map<int,Site> sites;
    int cell(int d,int axis,int64_t local) const;
    XY center(GRPoint p) const;
    std::vector<Edge> seedLanding(int net,XY target,const Edges& chosen,const std::set<int>& excluded = {}) const;
    std::vector<const Site*> sitesNearRegion(XY low,XY high,size_t count) const;
    double singleHbtLowerBound(GRPoint a,GRPoint b,const Edge& landing) const;
    std::vector<Edge> neighbors(GRPoint p,int net) const;
    void validate(int net,const Route& route) const;
    bool available(int net,const Edges& edges) const;
    bool evaluateCompact(int net,const std::vector<Edge>& edges,PathQuality& quality) const;
    double cost(const Edges& edges) const;
    // 对已由 neighbors() 给出的合法单位边，直接查询余量和静态价格。
    double stepCost(int net,const Edge&) const;
    double wireUm(const Edge& edge) const;
    Demand resources(const Edges& edges) const;
    double capacity(Resource id) const;
    ResourceLocation location(Resource id) const;
    Resource resourceId(const Edge& edge) const;
    Resource nodeId(GRPoint point) const {return pointId(point);}
    Resource nodeCount() const {return volume;}
    bool extendPath(int net,const Edges& path,Edges& tree,Demand& demand,std::vector<int>& hbt) const;
    PreparedRoute prepareRoute(int net,const Route& route) const;
    bool available(const RouteResourceSummary& prepared) const;
    void commitPrepared(int net,const PreparedRoute& prepared);
    bool replacePrepared(int net,const PreparedRoute& prepared);
    void commit(int net,const Route& route);
    void release(int net);
    const Occupancy& demand() const {return used;}
    Resource hbtResource(int site) const {return siteId(site);}
    std::vector<int> spacingNeighbors(int site) const;
    void searchPrices(Occupancy prices) {dynamicPrice=std::move(prices);}
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
    struct Footprint {
        double demand;
        bool low,high;
    };
    std::map<GRPoint,std::vector<Edge>> landings;
    RTree siteIndex;
    std::set<int> siteLines[2][2];
    std::map<int,std::vector<int>> fixedByNet;
    std::array<unsigned,2> l1Weights{{1,1}};
    double distancePrice;
    std::vector<std::vector<double>> layerLower;
    std::vector<std::vector<std::vector<GRPoint>>> access;
    std::array<GridGraphView<bool>,2> congestionView;
    void publishDemand(const RouteResourceSummary& changed);
    Occupancy used;
    Occupancy dynamicPrice;
    std::vector<PreparedRoute> owned;
    std::array<Resource,2> offsets{};
    Resource volume;
    std::array<std::vector<std::vector<unsigned>>,2> trackCounts;
    std::array<std::vector<std::vector<Footprint>>,2> footprints;
    std::array<std::vector<bool>,2> layerPairs;
    std::vector<double> staticCapacity;
    bool cacheStatic;
    int viaCapacity[2];
    double spacing,wireWeight,viaWeight,hbtWeight,congestionWeight,hbtPressureWeight,shortAreaWeight;
    bool allowed(int net,const Edge& e) const;
    Resource pointId(GRPoint p) const;
    Resource siteId(int id) const {return 2*volume+static_cast<unsigned>(id);}
    Footprint landingFootprint(GRPoint p) const;
    unsigned countTracks(int die,int layer,int position) const;
    template<class Range> Demand computeDemand(const Range&) const;
    template<class Range> bool collectHbtSites(int net,const Range& edges,std::vector<int>& out) const;
    bool availableSites(const std::vector<int>& sites,const std::vector<int>* released=nullptr) const;
    double resourcePrice(Resource,double cap,double demand) const;
    double extraPrice(Resource id,double demand=1.) const;
    template<class Range> double baseCost(const Range& edges,const Demand& demand) const;
    void validateConnectivity(int net,const Route& route) const;
};
}
