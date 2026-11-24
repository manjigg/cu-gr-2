#include "RoutingGraph.h"
#include <cmath>

namespace native3d {
static Resource key(const Edge& e) {
    if (e.type==GREdgeType::HBT) return {e.type,-1,-1,e.site,-1};
    return {e.type,e.a.dieIdx,e.a.layerIdx,e.a.x,e.a.y};
}
static double weight(const Json& j,const char* field) {
    double w=j.at(field).get<double>();
    if (!std::isfinite(w)||w<0) throw std::invalid_argument(field);
    return w;
}
int RoutingGraph::cell(int d,int axis,int64_t local) const {
    const auto& lines=model.dies.at(d)->design->getGridlines().at(axis);
    if(local<lines.front() || local>lines.back()) throw std::out_of_range("Landing outside die");
    return std::min<int>(lines.size()-2,std::upper_bound(lines.begin(),lines.end(),local)-lines.begin()-1);
}
XY RoutingGraph::center(GRPoint p) const {
    const auto& d=*model.dies.at(p.dieIdx);
    auto box=d.grid.getCellBox(p);
    return d.transform.bonded({{box.cx(),box.cy()}});
}
RoutingGraph::RoutingGraph(const MultiDieDesign& m):model(m) {
    const auto& j=m.config;
    const auto& b=j.at("bonding");
    if(b.count("l1_die_weights")) {
        auto weights=pairOf(b.at("l1_die_weights"));
        for(int d:{0,1}) {
            if(weights[d]<=0) throw std::invalid_argument("L1 weights must be positive");
            l1Weights[d]=static_cast<unsigned>(weights[d]);
        }
    }
    XY origin=pairOf(b.at("origin")), pitch=pairOf(b.at("pitch"));
    spacing=integer(b.at("min_spacing"));
    if (spacing<0 || pitch[0]<=0 || pitch[1]<=0) throw std::invalid_argument("Invalid HBT array");
    if(spacing<=std::min(pitch[0],pitch[1])) spacing=0; // 唯一阵列坐标天然满足间距。
    const auto& w=j.at("weights");
    wireWeight=weight(w,"wire_um"); viaWeight=weight(w,"via");
    hbtWeight=weight(w,"hbt"); congestionWeight=weight(w,"congestion");
    hbtPressureWeight=w.count("hbt_pressure")?weight(w,"hbt_pressure"):congestionWeight;
    shortAreaWeight=w.count("short_area_um2")?weight(w,"short_area_um2"):0.;
    distancePrice=wireWeight/m.units;
    for(int d=0;d<2;++d) {
        const auto& spec=j.at("dies").at(d).at("via_capacity");
        if(spec.is_string() && spec.get<std::string>()=="track_intersections") viaCapacity[d]=-1;
        else {
            viaCapacity[d]=integer(spec);
            if(viaCapacity[d]<0) throw std::invalid_argument("Via capacity must be nonnegative");
        }
    }
    std::set<XY> positions;
    std::vector<std::pair<XY,XY>> keepouts;
    for(const auto& box:b.at("keepouts")) {
        XY lo=pairOf(box.at(0)),hi=pairOf(box.at(1));
        if(lo[0]>hi[0]||lo[1]>hi[1]) throw std::invalid_argument("Inverted keepout");
        keepouts.emplace_back(lo,hi);
    }
    for(const auto& item:b.at("sites")) {
        Site s;
        s.id=integer(item.at("id")); s.bonded=pairOf(item.at("xy"));
        s.capacity=integer(item.at("capacity")); s.owner=item.count("fixed_net")?integer(item.at("fixed_net")):-1;
        if(s.id<0 || sites.count(s.id) || !positions.insert(s.bonded).second || s.capacity<1 ||
           s.owner < -1 || s.owner>=static_cast<int>(m.nets.size())) throw std::invalid_argument("Invalid physical HBT");
        for(int axis=0;axis<2;++axis) if ((s.bonded[axis]-origin[axis])%pitch[axis])
            throw std::invalid_argument("HBT off array");
        bool blocked=false;
        for(const auto& r:keepouts) blocked|=r.first[0]<=s.bonded[0] && s.bonded[0]<=r.second[0] &&
                                               r.first[1]<=s.bonded[1] && s.bonded[1]<=r.second[1];
        if(blocked && s.owner>=0) throw std::invalid_argument("Fixed HBT in keepout");
        sites.emplace(s.id,s);
        if(s.owner>=0) fixedByNet[s.owner].push_back(s.id);
        auto& stored=sites.at(s.id);
        for(int d=0;d<2;++d) stored.local[d]=m.dies[d]->transform.local(s.bonded);
        for(const auto& pair:item.at("layers")) {
            GRPoint pts[2]={{0,0,0},{0,0,0}};
            for(int d=0;d<2;++d) {
                const auto& grid=m.dies[d]->grid;
                int l=0;
                while(l<int(grid.getNumLayers()) && grid.getLayerName(l)!=pair.at(d).get<std::string>()) ++l;
                if(l==int(grid.getNumLayers())) throw std::invalid_argument("Unknown landing layer");
                pts[d]=GRPoint(d,l,cell(d,0,stored.local[d][0]),cell(d,1,stored.local[d][1]));
            }
            Edge edge(pts[0],pts[1],GREdgeType::HBT,s.id);
            if(!blocked) {
                stored.transitions.push_back(edge);
                for(auto p:pts) {
                    landings[p].push_back(edge);
                    siteLines[p.dieIdx][0].insert(p.x);siteLines[p.dieIdx][1].insert(p.y);
                }
                auto a=center(pts[0]),b=center(pts[1]);
                auto jump=std::abs(a[0]-b[0])+std::abs(a[1]-b[1]);
                if(jump) distancePrice=std::min(distancePrice,hbtWeight/jump);
            }
        }
        if(!blocked) {
            boostPoint xy(stored.bonded[0],stored.bonded[1]);
            siteIndex.insert({boostBox(xy,xy),stored.id});
        }
        if(!blocked && stored.transitions.empty()) throw std::invalid_argument("Missing landing pair");
    }
    // 收缩 XY 得到 Die/层上的剩余代价下界；与格中心位移下界不重复相加。
    const int layers=m.dies[0]->grid.getNumLayers()+m.dies[1]->grid.getNumLayers();
    layerLower.assign(layers,std::vector<double>(layers,std::numeric_limits<double>::infinity()));
    auto lowerEdge=[&](int a,int b,double price) {
        layerLower[a][b]=layerLower[b][a]=std::min(layerLower[a][b],price);
    };
    for(int d=0;d<2;++d) for(int l=0;l<int(m.dies[d]->grid.getNumLayers());++l) {
        int i=stateIndex({d,l,0,0});layerLower[i][i]=0;
        if(l) lowerEdge(i,i-1,viaWeight);
    }
    for(const auto& s:sites) for(const auto& edge:s.second.transitions) {
        auto a=center(edge.a),b=center(edge.b);
        double jump=std::abs(a[0]-b[0])+std::abs(a[1]-b[1]);
        lowerEdge(stateIndex(edge.a),stateIndex(edge.b),std::max(0.,hbtWeight-jump*distancePrice));
    }
    for(int k=0;k<layers;++k) for(int i=0;i<layers;++i) for(int j=0;j<layers;++j)
        layerLower[i][j]=std::min(layerLower[i][j],layerLower[i][k]+layerLower[k][j]);
    access.resize(m.nets.size());
    for(const auto& n:m.nets) {
        for(int d=0;d<2;++d) if(n.localNet[d]>=0) {
            const auto& die=*m.dies[d];
            GRNet local(die.design->getAllNets().at(n.localNet[d]),*die.design,die.grid,d);
            for(const auto& pin:n.pins) if(pin.die==d && !pin.mappingPort) {
                const auto& ap=local.getPinAccessPoints().at(pin.ordinal);
                if(ap.empty()) throw std::invalid_argument("Terminal without legal access point");
                access[n.id].push_back(ap);
            }
        }
    }
}
bool RoutingGraph::validPoint(GRPoint p) const {
    if(p.dieIdx<0 || p.dieIdx>1) return false;
    const auto& g=model.dies[p.dieIdx]->grid;
    return p.x>=0 && p.y>=0 && p.layerIdx>=0 && p.x<int(g.getSize(0)) &&
           p.y<int(g.getSize(1)) && p.layerIdx<int(g.getNumLayers());
}
bool RoutingGraph::allowed(int net,const Edge& e) const {
    if(!validPoint(e.a)||!validPoint(e.b)) return false;
    if(e.type==GREdgeType::HBT) {
        auto it=sites.find(e.site);
        if(it==sites.end()||(it->second.owner>=0 && it->second.owner!=net)) return false;
        for(const auto& t:it->second.transitions) if(t.a==e.a && t.b==e.b) return true;
        return false;
    }
    if(e.site!=-1 || e.a.dieIdx!=e.b.dieIdx) return false;
    const auto& die=*model.dies[e.a.dieIdx];
    if(e.type==GREdgeType::VIA) return e.a.x==e.b.x && e.a.y==e.b.y &&
        e.b.layerIdx==e.a.layerIdx+1 && die.design->viaTypes().count({e.a.layerIdx,e.b.layerIdx});
    auto dir=die.grid.getLayerDirection(e.a.layerIdx);
    return e.a.layerIdx==e.b.layerIdx && e.a.layerIdx>=1 && e.a[dir]+1==e.b[dir] &&
        e.a[1-dir]==e.b[1-dir];
}
std::vector<Edge> RoutingGraph::seedLanding(int net,XY target,const Edges& chosen,const std::set<int>& excluded) const {
    // 只给初解选择最近可用站点。精确 DP 独立枚举全阵列。
    if(siteIndex.empty()) return {};
    boostPoint point(target[0],target[1]);
    for(auto it=siteIndex.qbegin(bgi::nearest(point,siteIndex.size()));it!=siteIndex.qend();++it) {
        if(excluded.count(it->second)) continue;
        for(const auto& h:sites.at(it->second).transitions) {
            Edges test=chosen;test.insert(h);
            if(available(net,test)) return {h};
        }
    }
    return {};
}
std::vector<Edge> RoutingGraph::neighbors(GRPoint p,int net) const {
    if(!validPoint(p)) throw std::out_of_range("Invalid search state");
    std::vector<Edge> out;
    auto insert=[&](const Edge& e){if(allowed(net,e)) out.push_back(e);};
    int dir=model.dies[p.dieIdx]->grid.getLayerDirection(p.layerIdx);
    for(int k:{-1,1}) {
        GRPoint q=p; q[dir]+=k; insert(Edge(p,q,GREdgeType::WIRE));
        q=p; q.layerIdx+=k; insert(Edge(p,q,GREdgeType::VIA));
    }
    auto it=landings.find(p);
    if(it!=landings.end()) for(const auto& e:it->second) insert(e);
    return out;
}
double RoutingGraph::wireUm(const Edge& e) const {
    const auto& g=model.dies[e.a.dieIdx]->grid;
    auto dir=g.getLayerDirection(e.a.layerIdx);
    return double(g.getEdgeLength(dir,e.a[dir]))/g.getLibDBU();
}
Demand RoutingGraph::resources(const Edges& edges) const {
    Demand res;
    for(const auto& e:edges) {
        res[key(e)]=res[key(e)]+1;
        if(e.type!=GREdgeType::VIA) continue;
        const auto& die=*model.dies[e.a.dieIdx];
        for(auto p:{e.a,e.b}) {
            auto dir=die.grid.getLayerDirection(p.layerIdx);
            int idx=p[dir], size=die.grid.getSize(dir);
            int lo=idx>0?die.grid.getEdgeLength(dir,idx-1):0;
            int hi=idx+1<size?die.grid.getEdgeLength(dir,idx):0;
            if(lo+hi==0) throw std::invalid_argument("Via has no adjacent landing segment");
            double footprint=2.0*die.design->getLayer(p.layerIdx).getMinLength()/(lo+hi);
            for(int delta:{-1,1}) if((delta<0?lo:hi) && footprint>0) {
                GRPoint q=p; q[dir]+=delta;
                res[key(Edge(p,q,GREdgeType::WIRE))]+=footprint;
            }
        }
    }
    return res;
}
double RoutingGraph::capacity(const Resource& id) const {
    auto d=std::get<1>(id), l=std::get<2>(id), x=std::get<3>(id), y=std::get<4>(id);
    switch(std::get<0>(id)) {
    case GREdgeType::HBT: return sites.at(x).capacity;
    case GREdgeType::VIA: {
        if(viaCapacity[d]>=0) return viaCapacity[d];
        // 全局 Via 容量代理：二层真实优选轨道在本 GCell 内的交点数。
        // 单独的金属着陆需求继续受障碍后的导线容量约束。
        const auto& die=*model.dies[d];
        const auto box=die.grid.getCellBox({x,y});
        double count=1;
        if(die.grid.getLayerDirection(l)==die.grid.getLayerDirection(l+1))
            throw std::invalid_argument("Parallel layers require explicit via capacity");
        for(int idx:{l,l+1}) {
            const auto& layer=die.design->getLayer(idx);
            int axis=1-layer.getDirection();
            auto interval=box[axis];
            if((axis==0?x:y)+1<int(die.grid.getSize(axis))) --interval.high;
            auto tracks=layer.rangeSearchTracks(interval);
            count*=tracks.IsValid()?tracks.range()+1:0;
        }
        return count;
    }
    default: return model.dies.at(d)->grid.getEdge(l,x,y).capacity;
    }
}
bool RoutingGraph::available(int net,const Edges& edges) const {
    std::set<int> chosen;
    for(const auto& e:edges) {
        if(!allowed(net,e)) return false;
        if(e.type!=GREdgeType::HBT) continue;
        if(!chosen.insert(e.site).second || used.count(key(e))) return false;
    }
    if(spacing>0) for(int id:chosen) for(const auto& item:sites) {
        if(item.first==id) continue;
        Resource other{GREdgeType::HBT,-1,-1,item.first,-1};
        if(!chosen.count(item.first) && !used.count(other) && item.second.owner<0) continue;
        auto a=sites.at(id).bonded,b=item.second.bonded;
        if(std::hypot(double(a[0]-b[0]),double(a[1]-b[1]))<spacing) return false;
    }
    for(const auto& r:resources(edges)) {
        auto it=used.find(r.first);
        if(r.second+(it==used.end()?0:it->second)>capacity(r.first)+1e-12) return false;
    }
    return true;
}
double RoutingGraph::cost(const Edges& edges) const {
    double value=0;
    for(const auto& e:edges)
        value+=e.type==GREdgeType::WIRE?wireWeight*wireUm(e):e.type==GREdgeType::VIA?viaWeight:hbtWeight;
    for(const auto& r:resources(edges)) value+=resourcePrice(r.first,capacity(r.first),r.second);
    return value;
}
double RoutingGraph::resourcePrice(const Resource& id,double cap,double demand) const {
    auto type=std::get<0>(id);
    double value=(type==GREdgeType::HBT?hbtPressureWeight:congestionWeight)*demand/(1+cap);
    if(shortAreaWeight>0 && type==GREdgeType::WIRE) {
        int d=std::get<1>(id),l=std::get<2>(id);
        const auto& die=*model.dies[d];
        int axis=die.grid.getLayerDirection(l);
        double um=double(die.grid.getEdgeLength(axis,axis==0?std::get<3>(id):std::get<4>(id)))/die.grid.getLibDBU();
        double width=double(die.design->getLayer(l).getWidth())/die.grid.getLibDBU();
        double probability=cap<1?1.:1./(1.+std::exp(cap*model.parameters.cost_logistic_slope));
        value+=shortAreaWeight*um*width*demand*probability;
    }
    return value;
}
double RoutingGraph::stepCost(int net,const Edge& edge) const {
    const auto inf=std::numeric_limits<double>::infinity();
    if(edge.type==GREdgeType::HBT) return available(net,{edge})?cost({edge}):inf;
    const auto& die=*model.dies[edge.a.dieIdx];
    auto bounds=[&](GRPoint p,double demand) {
        const auto r=die.grid.getEdge(p.layerIdx,p.x,p.y);
        if(r.getResource()+1e-12<demand) return inf;
        return resourcePrice({GREdgeType::WIRE,p.dieIdx,p.layerIdx,p.x,p.y},r.capacity,demand);
    };
    if(edge.type==GREdgeType::WIRE) return wireWeight*wireUm(edge)+bounds(edge.a,1);
    auto id=key(edge);
    double cap=capacity(id);
    auto it=used.find(id);
    if((it==used.end()?0:it->second)+1>cap+1e-12) return inf;
    double total=viaWeight+resourcePrice(id,cap,1);
    for(auto p:{edge.a,edge.b}) {
        int axis=die.grid.getLayerDirection(p.layerIdx);
        int low=p[axis]?die.grid.getEdgeLength(axis,p[axis]-1):0;
        int high=p[axis]+1<int(die.grid.getSize(axis))?die.grid.getEdgeLength(axis,p[axis]):0;
        if(low+high==0) return inf;
        double demand=die.design->getLayer(p.layerIdx).getMinLength()*model.parameters.via_multiplier/(low+high);
        if(demand==0) continue;
        if(high) total+=bounds(p,demand);
        if(low) {--p[axis];total+=bounds(p,demand);}
    }
    return total;
}
void RoutingGraph::validate(int net,const Route& route) const {
    if(!validPoint(route.root)||!available(net,route.edges)) throw std::invalid_argument("Infeasible route resources");
    std::map<GRPoint,std::vector<GRPoint>> adj;
    for(const auto& e:route.edges){adj[e.a].push_back(e.b);adj[e.b].push_back(e.a);}
    std::set<GRPoint> seen{route.root};
    std::vector<GRPoint> todo{route.root};
    while(!todo.empty()) {
        auto p=todo.back(); todo.pop_back();
        for(auto q:adj[p]) if(seen.insert(q).second) todo.push_back(q);
    }
    if(seen.size()!=adj.size() || route.edges.size()+1!=seen.size()) throw std::invalid_argument("Disconnected or cyclic tree");
    for(const auto& pin:terminals(net))
        if(std::none_of(pin.begin(),pin.end(),[&](GRPoint p){return seen.count(p);})) throw std::invalid_argument("Detached terminal");
    auto fixed=fixedByNet.find(net);
    if(fixed!=fixedByNet.end()) for(int id:fixed->second)
        if(std::none_of(route.edges.begin(),route.edges.end(),[&](const Edge& e){return e.type==GREdgeType::HBT && e.site==id;}))
            throw std::invalid_argument("Fixed site missing");
}
void RoutingGraph::commit(int net,const Route& route) {
    if(owned.count(net)) throw std::logic_error("Double commit");
    validate(net,route);
    auto delta=resources(route.edges);
    // 只预分配本次涉及的键。成功取得全部内存后，发布过程无需分配。
    std::vector<Resource> inserted;
    inserted.reserve(delta.size());
    auto changed=owned.emplace(net,std::move(delta));
    try {
        for(const auto& r:changed.first->second)
            if(used.emplace(r.first,0.).second) inserted.push_back(r.first);
    } catch(...) {
        for(const auto& key:inserted) used.erase(key);
        owned.erase(changed.first);
        throw;
    }
    for(const auto& r:changed.first->second) used.at(r.first)+=r.second;
    publishDemand(changed.first->second);
}
void RoutingGraph::release(int net) {
    auto owner=owned.find(net);
    if(owner==owned.end()) throw std::logic_error("Release by non-owner");
    for(const auto& r:owner->second) {
        auto it=used.find(r.first);
        if(it==used.end() || it->second+1e-12<r.second) throw std::logic_error("Inconsistent resource ledger");
    }
    for(const auto& r:owner->second) {
        auto it=used.find(r.first);
        it->second-=r.second;
        if(std::abs(it->second)<1e-12) used.erase(it);
    }
    publishDemand(owner->second); owned.erase(owner);
}

void RoutingGraph::publishDemand(const Demand& changed) {
    for(const auto& r:changed) if(std::get<0>(r.first)==GREdgeType::WIRE) {
        auto it=used.find(r.first);
        model.dies[std::get<1>(r.first)]->grid.setWireDemand(std::get<2>(r.first),
            std::get<3>(r.first),std::get<4>(r.first),it==used.end()?0:it->second);
    }
}
void RoutingGraph::captureCongestion() {
    for(int d=0;d<2;++d) {
        const auto& grid=model.dies[d]->grid;
        grid.extractCongestionView(congestionView[d]);
        for(unsigned l=1;l<grid.getNumLayers();++l) {
            auto dir=grid.getLayerDirection(l);
            for(unsigned x=0;x<grid.getSize(0)-(dir==0);++x)
                for(unsigned y=0;y<grid.getSize(1)-(dir==1);++y)
                    if(grid.getEdge(l,x,y).getResource()<1)
                        congestionView[d][dir][x][y]=true;
        }
    }
}
}
