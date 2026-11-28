#include "RoutingGraph.h"
#include "Profile.h"
#include <cmath>
#include <unordered_set>

namespace native3d {
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
    indexOwners=j.value("enable_failure_driven_repair",false) || j.value("enable_topology_exchange",false);
    const auto& b=j.at("bonding");
    coupled=j.value("enable_hbt_footprint",false);
    if(j.value("enable_hbt_access_model",false)) accessWeight=weight(j,"hbt_access_weight");
    using LayerPair=std::array<std::string,2>;
    auto pairName=[](const Json& pair)->LayerPair {
        if(!pair.is_array() || pair.size()!=2) throw std::invalid_argument("Invalid HBT layer pair");
        return {{pair.at(0).get<std::string>(),pair.at(1).get<std::string>()}};
    };
    struct Reservation {int die,dl,dx,dy;GREdgeType type;double demand;};
    std::map<LayerPair,std::vector<Reservation>> rules;
    if(coupled) {
        const auto& table=b.at("transition_footprints");
        if(table.at("source").get<std::string>().empty()) throw std::invalid_argument("Missing HBT footprint provenance");
        for(const auto& rule:table.at("rules")) {
            const auto layers=pairName(rule.at("layers"));
            if(rules.count(layers)) throw std::invalid_argument("Duplicate HBT footprint layer pair");
            auto& rows=rules[layers];
            for(const auto& item:rule.at("reservations")) {
                const auto off=pairOf(item.at("offset"));
                const int die=integer(item.at("die")),dl=integer(item.at("layer_offset"));
                auto type=item.at("kind").get<std::string>();
                const double amount=weight(item,"demand");
                if((die!=0 && die!=1) || (type!="WIRE" && type!="VIA") || amount<=0)
                    throw std::invalid_argument("Invalid HBT landing reservation");
                rows.push_back({die,dl,int(off[0]),int(off[1]),type=="WIRE"?GREdgeType::WIRE:GREdgeType::VIA,amount});
            }
        }
    }
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
    cacheStatic=j.value("enable_resource_cache",true);
    volume=0;
    for(int d:{0,1}) {
        const auto& die=*m.dies[d];
        const auto& grid=die.grid;
        offsets[d]=volume;
        auto plane=Resource(grid.getSize(0))*grid.getSize(1);
        const auto layers=grid.getNumLayers();
        const auto maximum=(std::numeric_limits<Resource>::max()-INT_MAX)/2;
        if(!plane || !layers || plane>(maximum-volume)/layers)
            throw std::overflow_error("Routing resource ID overflow");
        volume+=plane*layers;
        if(!cacheStatic) continue;
        trackCounts[d].resize(layers);
        footprints[d].resize(layers);
        layerPairs[d].resize(layers);
        for(unsigned l=0;l<layers;++l) {
            layerPairs[d][l]=die.design->viaTypes().count({l,l+1});
            const auto axis=grid.getLayerDirection(l);
            if(viaCapacity[d]<0) for(unsigned pos=0;pos<grid.getSize(1-axis);++pos)
                trackCounts[d][l].push_back(countTracks(d,l,pos));
            auto& values=footprints[d][l];
            values.reserve(grid.getSize(axis));
            for(unsigned pos=0;pos<grid.getSize(axis);++pos) {
                GRPoint point(d,l,0,0);
                point[axis]=pos;
                values.push_back(landingFootprint(point));
            }
        }
    }
    // 上限约 40 MiB。更大的网格继续使用逐轴轨道缓存，避免按整个空间分配。
    if(cacheStatic && volume<=2500000) {
        std::vector<double> values(2*volume);
        for(Resource id=0;id<2*volume;++id) {
            const auto loc=location(id);
            if(loc.type==GREdgeType::VIA && !layerPairs[loc.die][loc.layer]) continue;
            values[id]=capacity(id);
        }
        staticCapacity.swap(values);
    }
    owned.resize(m.nets.size());
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
                if(coupled) {
                    const auto rule=rules.find(pairName(pair));
                    if(rule==rules.end()) throw std::invalid_argument("Missing HBT landing reservation rule");
                    Demand extra;
                    bool fits=true;
                    for(const auto& r:rule->second) {
                        auto p=pts[r.die];
                        const auto& grid=m.dies[r.die]->grid;
                        const int64_t x=int64_t(p.x)+r.dx,y=int64_t(p.y)+r.dy,l=int64_t(p.layerIdx)+r.dl;
                        if(x<0 || x>=grid.getSize(0) || y<0 || y>=grid.getSize(1) || l<0 || l>=grid.getNumLayers()) {fits=false;break;}
                        p=GRPoint(r.die,int(l),int(x),int(y));
                        auto q=p;
                        if(r.type==GREdgeType::WIRE) ++q[grid.getLayerDirection(p.layerIdx)];
                        else ++q.layerIdx;
                        const Edge reserved(p,q,r.type);
                        if(!allowed(-1,reserved)) {fits=false;break;}
                        extra[resourceId(reserved)]+=r.demand;
                    }
                    // 越界或非法层对使这一种着陆失效，其他真实层对仍可独立使用。
                    if(!fits) continue;
                    hbtFootprints.emplace(edge,std::vector<std::pair<Resource,double>>(extra.begin(),extra.end()));
                }
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
        if(!stored.transitions.empty()) {
            boostPoint xy(stored.bonded[0],stored.bonded[1]);
            siteIndex.insert({boostBox(xy,xy),stored.id});
        }
        if(!blocked && stored.transitions.empty() && (!coupled || s.owner>=0 || item.at("layers").empty()))
            throw std::invalid_argument("Missing legal landing pair");
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
        e.b.layerIdx==e.a.layerIdx+1 &&
        (cacheStatic?layerPairs[e.a.dieIdx][e.a.layerIdx]:die.design->viaTypes().count({e.a.layerIdx,e.b.layerIdx}));
    auto dir=die.grid.getLayerDirection(e.a.layerIdx);
    return e.a.layerIdx==e.b.layerIdx && e.a.layerIdx>=1 && e.a[dir]+1==e.b[dir] &&
        e.a[1-dir]==e.b[1-dir];
}
std::vector<Edge> RoutingGraph::seedLanding(int net,XY target,const Edges& chosen,const std::set<int>& excluded) const {
    Measured measured(Timing::HbtSearch);
    // 只给初解选择最近可用站点。精确 DP 独立枚举全阵列。
    if(siteIndex.empty()) return {};
    const bool incremental=model.config.value("enable_incremental_seed_sites",false) && !coupled &&
        std::all_of(chosen.begin(),chosen.end(),[](const Edge& e){return e.type==GREdgeType::HBT;});
    std::set<int> chosenIds;
    if(incremental) {
        if(!chosen.empty() && !available(net,chosen)) return {};
        for(const auto& edge:chosen) chosenIds.insert(edge.site);
    }
    auto canAppend=[&](const Edge& h) {
        if(!incremental) {
            auto test=chosen;test.insert(h);
            return available(net,test);
        }
        // 整个函数内外部占用不变。已有同一物理边的并集不增加需求；
        // 同站点采用不同着陆层对、间距冲突和其他网占用仍须明确拒绝。
        if(chosen.count(h)) return true;
        if(chosenIds.count(h.site) || !available(net,{h})) return false;
        for(auto conflict:spacingNeighbors(h.site)) if(chosenIds.count(conflict)) return false;
        return true;
    };
    boostPoint point(target[0],target[1]);
    if(accessWeight>0) {
        for(size_t k=std::min<size_t>(8,siteIndex.size());;k=std::min(2*k,siteIndex.size())) {
            std::pair<double,int> best{std::numeric_limits<double>::infinity(),-1};
            std::vector<Edge> first;
            for(const auto* site:sitesNearRegion(target,target,k)) {
                if(excluded.count(site->id)) continue;
                const double distance=std::abs(site->bonded[0]-target[0])+std::abs(site->bonded[1]-target[1]);
                for(const auto& h:site->transitions) {
                    if(!canAppend(h)) continue;
                    const auto key=std::make_pair(distance*wireWeight/model.units+landingRisk(h),site->id);
                    if(key<best) {best=key;first={h};}
                }
            }
            if(!first.empty() || k==siteIndex.size()) return first;
        }
    }
    if(model.config.value("routing_mode",std::string("exact"))=="fast" &&
       model.config.value("enable_hbt_index",true)) {
        const bool preserveTies=model.config.value("preserve_seed_ties",true);
        std::set<int> visited=excluded;
        auto distance=[&](int id) {
            const auto xy=sites.at(id).bonded;
            long double dx=xy[0]-target[0],dy=xy[1]-target[1];
            return dx*dx+dy*dy;
        };
        std::vector<Edge> first;
        long double nearest=0;
        // 小规模近邻优先。都被占用时扩大查询，最终覆盖完整合法阵列。
        size_t limit=std::min<size_t>(8,siteIndex.size());
        bool tie=false;
        while(true) {
            for(auto it=siteIndex.qbegin(bgi::nearest(point,limit));it!=siteIndex.qend();++it) {
                const auto score=distance(it->second);
                if(!first.empty() && score>nearest) return first;
                if(!visited.insert(it->second).second) continue;
                Profile::count(Event::HbtSites);
                for(const auto& h:sites.at(it->second).transitions) {
                    if(!canAppend(h)) continue;
                    if(!preserveTies) return {h};
                    if(!first.empty()) tie=true;
                    else {first.push_back(h);nearest=score;}
                    break;
                }
                if(tie) break;
            }
            // 确有两个等距可用站点时运行旧查询，以保留已有初解的并列决策次序。
            if(tie) break;
            if(limit==siteIndex.size()) return first;
            limit=std::min(siteIndex.size(),2*limit);
        }
    }
    for(auto it=siteIndex.qbegin(bgi::nearest(point,siteIndex.size()));it!=siteIndex.qend();++it) {
        Profile::count(Event::HbtSites);
        if(excluded.count(it->second)) continue;
        for(const auto& h:sites.at(it->second).transitions) {
            if(canAppend(h)) return {h};
        }
    }
    return {};
}
std::vector<const Site*> RoutingGraph::sitesNearRegion(XY low,XY high,size_t count) const {
    std::vector<const Site*> result;
    if(!count || siteIndex.empty()) return result;
    auto bound=[](int64_t x){return DBU(std::max<int64_t>(INT_MIN,std::min<int64_t>(INT_MAX,x)));};
    const boostBox region(boostPoint(bound(low[0]),bound(low[1])),boostPoint(bound(high[0]),bound(high[1])));
    const auto target=std::min(count,siteIndex.size());
    result.reserve(target);
    for(auto it=siteIndex.qbegin(bgi::nearest(region,target));it!=siteIndex.qend();++it)
        result.push_back(&sites.at(it->second));
    return result;
}
double RoutingGraph::singleHbtLowerBound(GRPoint a,GRPoint b,const Edge& h) const {
    const GRPoint u=a.dieIdx?h.b:h.a,v=a.dieIdx?h.a:h.b;
    const auto pa=center(a),pb=center(b),pu=center(u),pv=center(v);
    double wire=0;
    for(int axis:{0,1}) wire+=std::abs(pa[axis]-pu[axis])+std::abs(pb[axis]-pv[axis]);
    // 端点至着陆格中心的两段线属于不同 Die。只计算独立的最低平面位移与换层次数。
    return hbtWeight+wire*wireWeight/model.units+
           viaWeight*(std::abs(a.layerIdx-u.layerIdx)+std::abs(b.layerIdx-v.layerIdx));
}
double RoutingGraph::landingRisk(const Edge& edge,const ResourceReadView* view) const {
    if(!accessWeight) return 0;
    double both=1.;
    for(auto p:{edge.a,edge.b}) {
        const auto& grid=model.dies[p.dieIdx]->grid;
        const int axis=grid.getLayerDirection(p.layerIdx);
        double cap=0,free=0;
        for(int sign:{-1,1}) {
            auto q=p;q[axis]+=sign;
            Edge wire(p,q,GREdgeType::WIRE);
            if(!allowed(-1,wire)) continue;
            const Resource id=resourceId(wire);
            const double c=capacity(id);
            cap+=c;free+=std::max(0.,c-externalDemand(id,view));
        }
        both=std::min(both,cap>0?free/cap:0.);
    }
    return accessWeight*(1.-both);
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
Resource RoutingGraph::pointId(GRPoint p) const {
    const auto& grid=model.dies[p.dieIdx]->grid;
    return offsets[p.dieIdx]+(Resource(p.layerIdx)*grid.getSize(0)+p.x)*grid.getSize(1)+p.y;
}
Resource RoutingGraph::resourceId(const Edge& edge) const {
    if(edge.type==GREdgeType::HBT) return siteId(edge.site);
    return pointId(edge.a)+(edge.type==GREdgeType::VIA?volume:0);
}
ResourceLocation RoutingGraph::location(Resource id) const {
    if(id>=2*volume) return {GREdgeType::HBT,-1,-1,int(id-2*volume),-1};
    const auto type=id>=volume?GREdgeType::VIA:GREdgeType::WIRE;
    if(type==GREdgeType::VIA) id-=volume;
    const int d=id>=offsets[1];
    id-=offsets[d];
    const auto& grid=model.dies[d]->grid;
    const int y=id%grid.getSize(1);
    id/=grid.getSize(1);
    return {type,d,int(id/grid.getSize(0)),int(id%grid.getSize(0)),y};
}
RoutingGraph::Footprint RoutingGraph::landingFootprint(GRPoint p) const {
    const auto& die=*model.dies[p.dieIdx];
    const auto dir=die.grid.getLayerDirection(p.layerIdx);
    const auto& cached=footprints[p.dieIdx];
    if(!cached.empty() && p[dir]<int(cached[p.layerIdx].size())) return cached[p.layerIdx][p[dir]];
    const int lo=p[dir]>0?die.grid.getEdgeLength(dir,p[dir]-1):0;
    const int hi=p[dir]+1<int(die.grid.getSize(dir))?die.grid.getEdgeLength(dir,p[dir]):0;
    if(lo+hi==0) return {0.,false,false};
    return {model.parameters.via_multiplier*die.design->getLayer(p.layerIdx).getMinLength()/(lo+hi),lo>0,hi>0};
}
template<class Range>
Demand RoutingGraph::computeDemand(const Range& edges) const {
    Measured measured(Timing::Resources);
    Demand res;
    for(const auto& edge:edges) {
        res[resourceId(edge)]+=1;
        if(coupled && edge.type==GREdgeType::HBT)
            for(const auto& item:hbtFootprints.at(edge)) res[item.first]+=item.second;
        if(edge.type!=GREdgeType::VIA) continue;
        for(auto p:{edge.a,edge.b}) {
            const auto footprint=landingFootprint(p);
            if(!footprint.high && !footprint.low)
                throw std::invalid_argument("Via has no adjacent landing segment");
            if(footprint.demand<=0) continue;
            if(footprint.high) res[pointId(p)]+=footprint.demand;
            if(footprint.low) {
                --p[model.dies[p.dieIdx]->grid.getLayerDirection(p.layerIdx)];
                res[pointId(p)]+=footprint.demand;
            }
        }
    }
    return res;
}
Demand RoutingGraph::resources(const Edges& edges) const {return computeDemand(edges);}
unsigned RoutingGraph::countTracks(int d,int l,int pos) const {
    const auto& die=*model.dies[d];
    const auto& layer=die.design->getLayer(l);
    const int axis=1-layer.getDirection();
    auto interval=utils::IntervalT<DBU>(die.grid.getGridline(axis,pos),die.grid.getGridline(axis,pos+1));
    if(pos+1<int(die.grid.getSize(axis))) --interval.high;
    auto tracks=layer.rangeSearchTracks(interval);
    return tracks.IsValid()?tracks.range()+1:0;
}
double RoutingGraph::capacity(Resource id) const {
    Measured measured(Timing::Capacity);
    if(id<staticCapacity.size()) return staticCapacity[id];
    const auto loc=location(id);
    if(loc.type==GREdgeType::HBT) return sites.at(loc.x).capacity;
    const int d=loc.die,l=loc.layer;
    const auto& grid=model.dies[d]->grid;
    if(loc.type==GREdgeType::WIRE) return grid.getEdge(l,loc.x,loc.y).capacity;
    if(viaCapacity[d]>=0) return viaCapacity[d];
    if(grid.getLayerDirection(l)==grid.getLayerDirection(l+1))
        throw std::invalid_argument("Parallel layers require explicit via capacity");
    double count=1;
    for(int idx:{l,l+1}) {
        const int pos=grid.getLayerDirection(idx)==0?loc.y:loc.x;
        count*=cacheStatic?trackCounts[d][idx][pos]:countTracks(d,idx,pos);
    }
    return count;
}
template<class Range>
bool RoutingGraph::collectHbtSites(int net,const Range& edges,std::vector<int>& chosen) const {
    for(const auto& edge:edges) {
        if(!allowed(net,edge)) return false;
        if(edge.type==GREdgeType::HBT) chosen.push_back(edge.site);
    }
    std::sort(chosen.begin(),chosen.end());
    return std::adjacent_find(chosen.begin(),chosen.end())==chosen.end();
}
bool RoutingGraph::availableSites(const std::vector<int>& chosen,const std::vector<int>* released,
                                 const ResourceReadView* view,bool staticOnly) const {
    auto alreadyUsed=[&](int id) {
        if(staticOnly) return false;
        if(view && view->provisional && std::binary_search(view->provisional->sites.begin(),view->provisional->sites.end(),id)) return true;
        return used.count(siteId(id)) && !(released && std::binary_search(released->begin(),released->end(),id));
    };
    for(int id:chosen) {
        if(alreadyUsed(id)) {Profile::count(Event::HbtOccupied);return false;}
        for(int other:spacingNeighbors(id))
            if(std::binary_search(chosen.begin(),chosen.end(),other) || alreadyUsed(other) || sites.at(other).owner>=0) {
                Profile::count(Event::HbtSpacing);return false;
            }
    }
    return true;
}
std::vector<int> RoutingGraph::spacingNeighbors(int id) const {
    std::vector<int> candidates;
    if(spacing<=0) return candidates;
    const auto a=sites.at(id).bonded;
    auto bounded=[](double x){return static_cast<DBU>(std::max<double>(INT_MIN,std::min<double>(INT_MAX,x)));};
    const boostBox area(boostPoint(bounded(a[0]-spacing),bounded(a[1]-spacing)),
                        boostPoint(bounded(a[0]+spacing),bounded(a[1]+spacing)));
    for(auto q=siteIndex.qbegin(bgi::intersects(area));q!=siteIndex.qend();++q) {
        if(q->second==id) continue;
        const auto b=sites.at(q->second).bonded;
        if(std::hypot(double(a[0]-b[0]),double(a[1]-b[1]))<spacing) candidates.push_back(q->second);
    }
    return candidates;
}
void RoutingGraph::checkView(int net,const ResourceReadView* view) const {
    if(view && (view->graph!=this || view->net!=net || view->epoch!=ledgerVersion))
        throw std::logic_error("Stale resource view or incorrect net owner");
}
const std::vector<int>* RoutingGraph::releasedSites(const ResourceReadView* view) {
    return view && view->old?&view->old->hbtSites():nullptr;
}
double RoutingGraph::externalDemand(Resource id,const ResourceReadView* view) const {
    const auto it=used.find(id);
    double total=it==used.end()?0.:it->second;
    if(view && view->provisional) {
        const auto& added=view->provisional->demand;
        const auto row=std::lower_bound(added.begin(),added.end(),id,
            [](const std::pair<Resource,double>& r,Resource key){return r.first<key;});
        if(row!=added.end() && row->first==id) total+=row->second;
    }
    if(!view || !view->old) return total;
    const auto& demand=view->old->resources();
    const auto old=std::lower_bound(demand.begin(),demand.end(),id,
        [](const std::pair<Resource,double>& item,Resource key){return item.first<key;});
    if(old==demand.end() || old->first!=id) return total;
    const double remainder=total-old->second;
    if(remainder < -1e-12) throw std::logic_error("Released demand exceeds snapshot");
    return std::abs(remainder)<1e-12?0.:remainder;
}
bool RoutingGraph::available(int net,const Edges& edges,const ResourceReadView* view) const {
    checkView(net,view);
    Measured measured(Timing::Available);
    std::vector<int> chosen;
    if(!collectHbtSites(net,edges,chosen) || !availableSites(chosen,releasedSites(view),view)) return false;
    for(const auto& r:resources(edges)) {
        if(r.second+externalDemand(r.first,view)>capacity(r.first)+1e-12) return false;
    }
    return true;
}
bool RoutingGraph::available(const RouteResourceSummary& prepared,const ResourceReadView* view) const {
    checkView(prepared.net,view);
    Measured measured(Timing::Available);
    if(prepared.graph!=this) throw std::invalid_argument("Route summary belongs to another resource graph");
    if(!availableSites(prepared.sites,releasedSites(view),view)) return false;
    for(const auto& r:prepared.demand) {
        if(r.second+externalDemand(r.first,view)>capacity(r.first)+1e-12) return false;
    }
    return true;
}
bool RoutingGraph::evaluateCompact(int net,const std::vector<Edge>& edges,PathQuality& quality,const ResourceReadView* view) const {
    checkView(net,view);
    Measured measured(Timing::Available);
    quality=PathQuality{};
    if(!collectHbtSites(net,edges,quality.hbt) || !availableSites(quality.hbt,releasedSites(view),view)) return false;
    return scoreCompact(edges,computeDemand(edges),quality,view);
}
bool RoutingGraph::evaluateIncrement(int net,const std::vector<Edge>& parent,const Demand& parentDemand,
    const std::vector<Edge>& combined,Demand& result,PathQuality& quality) const {
    Measured measured(Timing::Available);
    quality=PathQuality{};
    if(!collectHbtSites(net,combined,quality.hbt) || !availableSites(quality.hbt)) return false;
    std::vector<Edge> added;
    std::set_difference(combined.begin(),combined.end(),parent.begin(),parent.end(),std::back_inserter(added));
    if(added.size()+parent.size()!=combined.size()) throw std::logic_error("Invalid incremental DP parent");
    auto delta=computeDemand(added);
    // 不相交的资源键在规范边序列中的加法次序完全相同。多条 Via 或 HBT 的金属预留
    // 命中同一资源时回退到原有的规范求和，保持浮点边界上的容量判断及标签排序一致。
    const bool overlap=std::any_of(delta.begin(),delta.end(),[&](const Demand::value_type& r){return parentDemand.count(r.first);});
    if(overlap) result=computeDemand(combined);
    else {result=parentDemand;result.insert(delta.begin(),delta.end());}
    return scoreCompact(combined,result,quality,nullptr);
}
bool RoutingGraph::scoreCompact(const std::vector<Edge>& edges,const Demand& demand,PathQuality& quality,const ResourceReadView* view) const {
    for(const auto& r:demand) {
        if(r.second+externalDemand(r.first,view)>capacity(r.first)+1e-12) return false;
    }
    quality.cost=baseCost(edges,demand);
    for(const auto& r:demand) quality.cost+=extraPrice(r.first,r.second);
    for(const auto& edge:edges) {
        if(edge.type==GREdgeType::VIA) ++quality.vias;
        if(edge.type==GREdgeType::WIRE) quality.wire+=wireUm(edge);
        if(accessWeight>0 && edge.type==GREdgeType::HBT) quality.cost+=landingRisk(edge,view);
    }
    return true;
}
bool RoutingGraph::extendPath(int net,const Edges& path,Edges& tree,Demand& demand,std::vector<int>& hbt,const ResourceReadView* view) const {
    checkView(net,view);
    Measured measured(Timing::Available);
    Edges added;
    std::set_difference(path.begin(),path.end(),tree.begin(),tree.end(),std::inserter(added,added.end()));
    auto nextHbt=hbt;
    if(!collectHbtSites(net,added,nextHbt) || !availableSites(nextHbt,releasedSites(view),view)) return false;
    const auto increase=computeDemand(added);
    for(const auto& r:increase) {
        const auto local=demand.find(r.first);
        const double total=r.second+(local==demand.end()?0.:local->second)+externalDemand(r.first,view);
        if(total>capacity(r.first)+1e-12) return false;
    }
    tree.insert(added.begin(),added.end());
    for(const auto& r:increase) demand[r.first]+=r.second;
    hbt.swap(nextHbt);
    return true;
}
template<class Range>
double RoutingGraph::baseCost(const Range& edges,const Demand& demand) const {
    double value=0;
    for(const auto& edge:edges)
        value+=edge.type==GREdgeType::WIRE?wireWeight*wireUm(edge):edge.type==GREdgeType::VIA?viaWeight:hbtWeight;
    for(const auto& r:demand) value+=resourcePrice(r.first,capacity(r.first),r.second);
    return value;
}
double RoutingGraph::cost(const Edges& edges) const {
    Measured measured(Timing::Cost);
    return baseCost(edges,resources(edges));
}
double RoutingGraph::resourcePrice(Resource id,double cap,double demand) const {
    const bool hbt=id>=2*volume;
    double value=(hbt?hbtPressureWeight:congestionWeight)*demand/(1+cap);
    if(shortAreaWeight>0 && id<volume) {
        const auto loc=location(id);
        const auto& die=*model.dies[loc.die];
        int axis=die.grid.getLayerDirection(loc.layer);
        double um=double(die.grid.getEdgeLength(axis,axis==0?loc.x:loc.y))/die.grid.getLibDBU();
        double width=double(die.design->getLayer(loc.layer).getWidth())/die.grid.getLibDBU();
        double probability=cap<1?1.:1./(1.+std::exp(cap*model.parameters.cost_logistic_slope));
        value+=shortAreaWeight*um*width*demand*probability;
    }
    return value;
}
double RoutingGraph::extraPrice(Resource id,double demand) const {
    if(dynamicPrice.empty()) return 0;
    const auto it=dynamicPrice.find(id);
    return it==dynamicPrice.end()?0:it->second*demand;
}
void RoutingGraph::priceBlockedRequests(const std::map<Resource,unsigned>& requests,double step,double decay,double ceiling) {
    if(!std::isfinite(step) || step<0 || !(decay>=0 && decay<=1) || !std::isfinite(ceiling) || ceiling<0)
        throw std::invalid_argument("Invalid failure pressure price parameters");
    // 被拒绝的搜索请求是竞争信号。价格只用于后续候选排序，基础代价和容量不变。
    for(const auto& item:requests) {
        auto& value=dynamicPrice[item.first];
        value=std::min(ceiling,decay*value+step*std::log1p(item.second));
    }
}
double RoutingGraph::stepCost(int net,const Edge& edge,const ResourceReadView* view) const {
    checkView(net,view);
    Measured measured(Timing::StepCost);
    const auto inf=std::numeric_limits<double>::infinity();
    if(edge.type==GREdgeType::HBT) {
        if(!available(net,{edge},view)) return inf;
        double price=cost({edge})+extraPrice(resourceId(edge))+landingRisk(edge,view);
        if(coupled) for(const auto& r:hbtFootprints.at(edge)) price+=extraPrice(r.first,r.second);
        return price;
    }
    const auto& die=*model.dies[edge.a.dieIdx];
    auto bounds=[&](GRPoint p,double demand) {
        const auto r=die.grid.getEdge(p.layerIdx,p.x,p.y);
        auto id=pointId(p);
        const double remaining=view?r.capacity-externalDemand(id,view):r.getResource();
        if(remaining+1e-12<demand) return inf;
        return resourcePrice(id,r.capacity,demand)+extraPrice(id,demand);
    };
    if(edge.type==GREdgeType::WIRE) return wireWeight*wireUm(edge)+bounds(edge.a,1);
    auto id=resourceId(edge);
    double cap=capacity(id);
    if(externalDemand(id,view)+1>cap+1e-12) return inf;
    double total=viaWeight+resourcePrice(id,cap,1)+extraPrice(id);
    for(auto p:{edge.a,edge.b}) {
        const auto footprint=landingFootprint(p);
        if(!footprint.low && !footprint.high) return inf;
        if(footprint.demand<=0) continue;
        if(footprint.high) total+=bounds(p,footprint.demand);
        if(footprint.low) {--p[die.grid.getLayerDirection(p.layerIdx)];total+=bounds(p,footprint.demand);}
    }
    return total;
}
void RoutingGraph::validateConnectivity(int net,const Route& route) const {
    std::unordered_map<Resource,std::vector<Resource>> adj;
    const auto root=pointId(route.root);
    adj[root];
    for(const auto& e:route.edges) {
        auto a=pointId(e.a),b=pointId(e.b);
        adj[a].push_back(b);adj[b].push_back(a);
    }
    if(route.edges.size()+1!=adj.size()) throw std::invalid_argument("Disconnected or cyclic tree");
    std::unordered_set<Resource> seen{root};
    std::vector<Resource> todo{root};
    while(!todo.empty()) {
        auto p=todo.back();todo.pop_back();
        for(auto q:adj.at(p)) if(seen.insert(q).second) todo.push_back(q);
    }
    if(seen.size()!=adj.size()) throw std::invalid_argument("Disconnected or cyclic tree");
    for(const auto& pin:terminals(net))
        if(std::none_of(pin.begin(),pin.end(),[&](GRPoint p){return seen.count(pointId(p));}))
            throw std::invalid_argument("Detached terminal");
    auto fixed=fixedByNet.find(net);
    if(fixed!=fixedByNet.end()) for(int id:fixed->second)
        if(std::none_of(route.edges.begin(),route.edges.end(),[&](const Edge& e){return e.type==GREdgeType::HBT && e.site==id;}))
            throw std::invalid_argument("Fixed site missing");
}
PreparedRoute RoutingGraph::prepareRoute(int net,const Route& route) const {
    Measured measured(Timing::Validate);
    std::shared_ptr<RouteResourceSummary> summary(new RouteResourceSummary(this,net));
    if(!validPoint(route.root) || !collectHbtSites(net,route.edges,summary->sites))
        throw std::invalid_argument("Infeasible route resources");
    validateConnectivity(net,route);
    const auto demand=resources(route.edges);
    summary->demand.assign(demand.begin(),demand.end());
    summary->baseCost=baseCost(route.edges,demand);
    for(const auto& edge:route.edges) {
        if(edge.type==GREdgeType::VIA) ++summary->viaCount;
        if(edge.type==GREdgeType::WIRE) summary->wireUm+=wireUm(edge);
    }
    return summary;
}
bool RoutingGraph::staticallyAvailable(const RouteResourceSummary& prepared) const {
    if(prepared.graph!=this) throw std::invalid_argument("Route summary belongs to another resource graph");
    return availableSites(prepared.sites,nullptr,nullptr,true) &&
           std::all_of(prepared.demand.begin(),prepared.demand.end(),[&](const auto& r) {
               return std::isfinite(r.second) && r.second>=0 && r.second<=capacity(r.first)+1e-12;
           });
}
void RoutingGraph::validate(int net,const Route& route,const ResourceReadView* view) const {
    checkView(net,view);
    Measured measured(Timing::Validate);
    if(!validPoint(route.root)||!available(net,route.edges,view)) throw std::invalid_argument("Infeasible route resources");
    validateConnectivity(net,route);
}
void RoutingGraph::commit(int net,const Route& route) {
    if(owned.at(net)) throw std::logic_error("Double commit");
    commitPrepared(net,prepareRoute(net,route));
}
// 先在私有副本中更新受影响资源的所有者。预留空键后，真正发布时只交换向量。
class RoutingGraph::OwnerEdit {
    OwnerMap& index;
    bool enabled;
    std::map<Resource,std::vector<int>> staged;
    std::vector<Resource> inserted;
public:
    explicit OwnerEdit(RoutingGraph& g):index(g.reverseOwners),enabled(g.indexOwners) {}
    void change(int net,const PreparedRoute& old,const PreparedRoute& next) {
        if(!enabled) return;
        static const std::vector<std::pair<Resource,double>> none;
        const auto& a=old?old->resources():none;
        const auto& b=next?next->resources():none;
        size_t i=0,j=0;
        while(i<a.size() || j<b.size()) {
            if(i<a.size() && j<b.size() && a[i].first==b[j].first) {++i;++j;continue;}
            const bool remove=j==b.size() || (i<a.size() && a[i].first<b[j].first);
            const auto key=remove?a[i++].first:b[j++].first;
            auto dest=staged.emplace(key,std::vector<int>{});
            if(dest.second && index.count(key)) dest.first->second=index.at(key);
            auto& owners=dest.first->second;
            auto pos=std::lower_bound(owners.begin(),owners.end(),net);
            if(remove) {
                if(pos==owners.end() || *pos!=net) throw std::logic_error("Missing resource owner");
                owners.erase(pos);
            } else {
                if(pos!=owners.end() && *pos==net) throw std::logic_error("Duplicate resource owner");
                owners.insert(pos,net);
            }
        }
    }
    void reserve() {
        inserted.reserve(staged.size());
        for(const auto& entry:staged) if(!entry.second.empty() && index.emplace(entry.first,std::vector<int>{}).second)
            inserted.push_back(entry.first);
    }
    void publish() {
        for(auto& entry:staged)
            if(entry.second.empty()) index.erase(entry.first);else index.at(entry.first).swap(entry.second);
        inserted.clear();
    }
    ~OwnerEdit() {for(auto id:inserted) index.erase(id);}
};
const std::vector<int>& RoutingGraph::users(Resource id) const {
    static const std::vector<int> empty;
    auto it=reverseOwners.find(id);
    return it==reverseOwners.end()?empty:it->second;
}
std::vector<Resource> RoutingGraph::blockingResources(int net,const Edge& edge,const ResourceReadView* view) const {
    checkView(net,view);
    std::vector<Resource> blocked;
    if(!allowed(net,edge)) return blocked;
    for(const auto& r:resources({edge}))
        if(!users(r.first).empty() && externalDemand(r.first,view)+r.second>capacity(r.first)+1e-12)
            blocked.push_back(r.first);
    if(edge.type==GREdgeType::HBT) {
        if(externalDemand(siteId(edge.site),view)>0) blocked.push_back(siteId(edge.site));
        for(int neighbor:spacingNeighbors(edge.site))
            if(externalDemand(siteId(neighbor),view)>0) blocked.push_back(siteId(neighbor));
    }
    std::sort(blocked.begin(),blocked.end());
    blocked.erase(std::unique(blocked.begin(),blocked.end()),blocked.end());
    return blocked;
}
template<class Range>
std::vector<BlockedResource> RoutingGraph::dynamicBlockers(const Range& demand,const std::vector<int>& sitesUsed,
                                                          const ResourceReadView& view) const {
    // 静态非法的请求不会派生交换任务。站点间距冲突保留请求站点和实际占用邻站。
    if(!availableSites(sitesUsed,nullptr,nullptr,true)) return {};
    for(const auto& r:demand) if(r.second>capacity(r.first)+1e-12) return {};
    std::vector<BlockedResource> blocked;
    const auto* removed=releasedSites(&view);
    auto active=[&](int site) {
        return used.count(siteId(site)) && !(removed && std::binary_search(removed->begin(),removed->end(),site));
    };
    for(const auto& r:demand)
        if(!users(r.first).empty() && r.second+externalDemand(r.first,&view)>capacity(r.first)+1e-12)
            blocked.push_back({r.first,r.first});
    for(int site:sitesUsed) {
        if(active(site)) blocked.push_back({siteId(site),siteId(site)});
        for(int neighbor:spacingNeighbors(site)) if(active(neighbor))
            blocked.push_back({siteId(site),siteId(neighbor)});
    }
    std::sort(blocked.begin(),blocked.end());
    blocked.erase(std::unique(blocked.begin(),blocked.end(),[](const BlockedResource& a,const BlockedResource& b){
        return !(a<b) && !(b<a);
    }),blocked.end());
    return blocked;
}
std::vector<BlockedResource> RoutingGraph::dynamicBlockers(int net,const Edges& edges,const ResourceReadView& view) const {
    checkView(net,&view);
    std::vector<int> sitesUsed;
    if(!collectHbtSites(net,edges,sitesUsed)) return {};
    return dynamicBlockers(resources(edges),sitesUsed,view);
}
std::vector<BlockedResource> RoutingGraph::dynamicBlockers(const RouteResourceSummary& route,
                                                          const ResourceReadView& view) const {
    checkView(route.net,&view);
    if(route.graph!=this) throw std::invalid_argument("Route summary belongs to another resource graph");
    return dynamicBlockers(route.demand,route.sites,view);
}
ResourceReadView RoutingGraph::groupView(int net,const std::vector<int>& group,const std::vector<PreparedRoute>& provisional) const {
    std::set<int> ids(group.begin(),group.end());
    if(ids.size()!=group.size() || !ids.count(net)) throw std::invalid_argument("Invalid resource view group");
    std::shared_ptr<RouteResourceSummary> summary(new RouteResourceSummary(this,net));
    Demand removed;
    for(int id:group) if(const auto& old=owned.at(id)) {
        for(const auto& item:old->demand) removed[item.first]+=item.second;
        summary->sites.insert(summary->sites.end(),old->sites.begin(),old->sites.end());
    }
    std::sort(summary->sites.begin(),summary->sites.end());
    summary->demand.assign(removed.begin(),removed.end());
    ResourceReadView view(this,net,ledgerVersion,std::move(summary));
    view.group=group;
    if(!provisional.empty()) {
        std::shared_ptr<RouteResourceSummary> extra(new RouteResourceSummary(this,net));
        Demand demand;
        for(const auto& route:provisional) {
            if(!route || route->graph!=this || !ids.erase(route->net) || route->net==net)
                throw std::invalid_argument("Invalid provisional group route");
            extra->sites.insert(extra->sites.end(),route->sites.begin(),route->sites.end());
            for(const auto& r:route->demand) demand[r.first]+=r.second;
        }
        extra->demand.assign(demand.begin(),demand.end());
        std::sort(extra->sites.begin(),extra->sites.end());
        view.provisional=std::move(extra);
    }
    return view;
}
bool RoutingGraph::groupDemand(const std::vector<int>& ids,const std::vector<PreparedRoute>& routes,
                               const ResourceReadView& view,Demand& additions) const {
    if(ids.empty() || ids.size()!=routes.size()) throw std::invalid_argument("Invalid route transaction group");
    checkView(ids.front(),&view);
    if(view.group!=ids || view.provisional) throw std::invalid_argument("Wrong group resource view");
    std::vector<int> sites;
    for(size_t i=0;i<ids.size();++i) {
        const auto& next=routes[i];
        if(!next || next->graph!=this || next->net!=ids[i]) throw std::invalid_argument("Invalid group route owner");
        sites.insert(sites.end(),next->sites.begin(),next->sites.end());
        for(const auto& r:next->demand) additions[r.first]+=r.second;
    }
    std::sort(sites.begin(),sites.end());
    if(std::adjacent_find(sites.begin(),sites.end())!=sites.end() || !availableSites(sites,releasedSites(&view))) return false;
    for(const auto& r:view.old->demand) additions.emplace(r.first,0.);
    for(auto& r:additions) {
        r.second+=externalDemand(r.first,&view);
        if(r.second>capacity(r.first)+1e-12) return false;
    }
    return true;
}
bool RoutingGraph::canReplaceGroup(const std::vector<int>& ids,const std::vector<PreparedRoute>& routes,
                                   const ResourceReadView& outside) const {
    Demand additions;
    return groupDemand(ids,routes,outside,additions);
}
bool RoutingGraph::replaceGroup(const std::vector<int>& ids,const std::vector<PreparedRoute>& routes) {
    if(ids.empty() || ids.size()!=routes.size()) throw std::invalid_argument("Invalid route transaction group");
    const auto view=groupView(ids.front(),ids);
    Demand additions;
    if(!groupDemand(ids,routes,view,additions)) return false;
    OwnerEdit index(*this);
    for(size_t i=0;i<ids.size();++i) index.change(ids[i],owned.at(ids[i]),routes[i]);
    index.reserve();
    std::vector<Resource> inserted;
    inserted.reserve(additions.size());
    try {
        for(const auto& r:additions) if(r.second>0 && used.emplace(r.first,0.).second) inserted.push_back(r.first);
    } catch(...) {for(auto id:inserted) used.erase(id);throw;}
    for(const auto& r:additions) {
        if(r.second>0) used.at(r.first)=r.second;else used.erase(r.first);
        if(r.first<volume) {
            const auto loc=location(r.first);
            model.dies[loc.die]->grid.setWireDemand(loc.layer,loc.x,loc.y,r.second);
        }
    }
    index.publish();
    for(size_t i=0;i<ids.size();++i) owned[ids[i]]=routes[i];
    ++ledgerVersion;
    return true;
}
void RoutingGraph::commitPrepared(int net,const PreparedRoute& prepared) {
    Measured measured(Timing::Commit);
    auto& owner=owned.at(net);
    if(owner) throw std::logic_error("Double commit");
    if(!prepared || net!=prepared->net || !available(*prepared)) throw std::invalid_argument("Infeasible route resources");
    OwnerEdit index(*this);index.change(net,nullptr,prepared);index.reserve();
    // 预分配全部键。提交期间没有新的内存分配，异常时只清理本次新增键。
    std::vector<Resource> inserted;
    inserted.reserve(prepared->demand.size());
    try {
        for(const auto& r:prepared->demand) if(!used.count(r.first)) {
            used.emplace(r.first,0.);
            inserted.push_back(r.first);
        }
    } catch(...) {
        for(auto id:inserted) used.erase(id);
        throw;
    }
    for(const auto& r:prepared->demand) used.at(r.first)+=r.second;
    publishDemand(*prepared);
    index.publish();
    owner=prepared;
    ++ledgerVersion;
}
bool RoutingGraph::replacePrepared(int net,const PreparedRoute& prepared) {
    auto& owner=owned.at(net);
    if(!owner) throw std::logic_error("Replacement by non-owner");
    if(!prepared || prepared->graph!=this || prepared->net!=net)
        throw std::invalid_argument("Invalid route summary for replacement");
    if(!availableSites(prepared->sites,&owner->sites)) return false;
    // 两个摘要都按资源 ID 排序。合并时先对新旧差量作全量预检，再发布变化。
    std::vector<std::pair<Resource,double>> updates;
    updates.reserve(owner->demand.size()+prepared->demand.size());
    auto a=owner->demand.begin(),b=prepared->demand.begin();
    while(a!=owner->demand.end() || b!=prepared->demand.end()) {
        const auto id=b==prepared->demand.end() || (a!=owner->demand.end() && a->first<b->first)?a->first:b->first;
        const double old=a!=owner->demand.end() && a->first==id?(a++)->second:0;
        const double next=b!=prepared->demand.end() && b->first==id?(b++)->second:0;
        if(old==next) continue;
        const auto occupied=used.find(id);
        double total=occupied==used.end()?0:occupied->second;
        if(total+1e-12<old) throw std::logic_error("Invalid replacement ledger");
        total=total-old+next;
        if(total>capacity(id)+1e-12) return false;
        updates.emplace_back(id,std::abs(total)<1e-12?0.:total);
    }
    OwnerEdit index(*this);index.change(net,owner,prepared);index.reserve();
    std::vector<Resource> inserted;
    inserted.reserve(updates.size());
    try {
        for(const auto& item:updates) if(item.second>0 && !used.count(item.first)) {
            used.emplace(item.first,0.);
            inserted.push_back(item.first);
        }
    } catch(...) {
        for(auto id:inserted) used.erase(id);
        throw;
    }
    for(const auto& item:updates) {
        if(item.second==0) used.erase(item.first);
        else used.at(item.first)=item.second;
        if(item.first<volume) {
            const auto loc=location(item.first);
            model.dies[loc.die]->grid.setWireDemand(loc.layer,loc.x,loc.y,item.second);
        }
    }
    index.publish();
    owner=prepared;
    ++ledgerVersion;
    return true;
}
void RoutingGraph::release(int net) {
    Measured measured(Timing::Release);
    auto& owner=owned.at(net);
    if(!owner) throw std::logic_error("Release by non-owner");
    OwnerEdit index(*this);index.change(net,owner,nullptr);index.reserve();
    for(const auto& r:owner->demand) {
        auto it=used.find(r.first);
        if(it==used.end() || it->second+1e-12<r.second) throw std::logic_error("Inconsistent resource ledger");
    }
    for(const auto& r:owner->demand) {
        auto it=used.find(r.first);
        it->second-=r.second;
        if(std::abs(it->second)<1e-12) used.erase(it);
    }
    publishDemand(*owner);
    index.publish();
    owner.reset();
    ++ledgerVersion;
}
void RoutingGraph::publishDemand(const RouteResourceSummary& changed) {
    for(const auto& r:changed.demand) {
        if(r.first>=volume) break;
        const auto loc=location(r.first);
        auto it=used.find(r.first);
        model.dies[loc.die]->grid.setWireDemand(loc.layer,loc.x,loc.y,it==used.end()?0:it->second);
    }
}
void RoutingGraph::restoreCheckpointDemand(const Occupancy& recorded) {
    if(used.size()!=recorded.size()) throw std::invalid_argument("Checkpoint resource keys disagree");
    for(const auto& r:recorded) {
        auto current=used.find(r.first);
        if(current==used.end() || !std::isfinite(r.second) || r.second<0 ||
           std::abs(current->second-r.second)>1e-10*std::max(1.,r.second) || r.second>capacity(r.first)+1e-12)
            throw std::invalid_argument("Checkpoint demand does not replay");
    }
    // 逐树回放通过后恢复冻结值的原始浮点位模式，消除提交顺序不同的加法舍入差异。
    for(const auto& r:recorded) {
        used.at(r.first)=r.second;
        if(r.first<volume) {
            auto loc=location(r.first);
            model.dies[loc.die]->grid.setWireDemand(loc.layer,loc.x,loc.y,r.second);
        }
    }
    ++ledgerVersion;
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
