#include "GuideOutput.h"
#include "gr/GlobalRouter.h"

namespace native3d {
static std::ofstream outputFile(const std::string& dir,const std::string& name) {
    std::ofstream os;
    os.exceptions(std::ios::failbit|std::ios::badbit);
    os.open(dir+"/"+name);
    return os;
}
Json writeSolution(const RoutingGraph& g,const std::vector<Route>& routes,const std::string& dir) {
    auto top=outputFile(dir,"top.guide"),bottom=outputFile(dir,"bottom.guide");
    auto hbt=outputFile(dir,"hbt_assignment.tsv"),components=outputFile(dir,"components.jsonl");
    auto treeLog=outputFile(dir,"route_edges.tsv");
    hbt<<"net_id\tsite_id\tbond_x\tbond_y\ttop_x\ttop_y\ttop_layer\ttop_component\tbottom_x\tbottom_y\tbottom_layer\tbottom_component\tfixed\n";
    treeLog<<"net_id\ttype\tsite_id\td0\tl0\tx0\ty0\td1\tl1\tx1\ty1\twire_um\n";
    treeLog.precision(17);
    double length[2]={0,0};
    size_t vias=0,assignments=0;
    std::set<int> usedSites;
    int pinArea=0,wireArea=0;
    for(const auto& net:g.model.nets) {
        const auto& tree=routes.at(net.id);
        std::map<GRPoint,std::vector<Edge>> adj;
        adj[tree.root];
        for(const auto& e:tree.edges) {
            auto& a=adj[e.a];auto& b=adj[e.b];
            double um=0;
            if(e.type!=GREdgeType::HBT) {
                a.push_back(e);b.push_back(e);
                if(e.type==GREdgeType::WIRE) {um=g.wireUm(e);length[e.a.dieIdx]+=um;}
                else ++vias;
            }
            treeLog<<net.id<<'\t'<<(e.type==GREdgeType::WIRE?"WIRE":e.type==GREdgeType::VIA?"VIA":"HBT")<<'\t'<<e.site;
            for(auto p:{e.a,e.b}) treeLog<<'\t'<<p.dieIdx<<'\t'<<p.layerIdx<<'\t'<<p.x<<'\t'<<p.y;
            treeLog<<'\t'<<um<<'\n';
        }
        std::map<GRPoint,int> group;
        std::vector<std::shared_ptr<GRTreeNode>> roots;
        for(const auto& item:adj) if(!group.count(item.first)) {
            int id=roots.size();
            auto root=std::make_shared<GRTreeNode>(item.first);
            roots.push_back(root);group.emplace(item.first,id);
            std::vector<std::shared_ptr<GRTreeNode>> stack{root};
            while(!stack.empty()) {
                auto p=stack.back();stack.pop_back();
                for(const auto& edge:adj.at(*p)) {
                    auto q=(*p==edge.a?edge.b:edge.a);
                    if(!group.emplace(q,id).second) continue;
                    auto child=std::make_shared<GRTreeNode>(q);
                    p->attach(child,edge.type);stack.push_back(child);
                }
            }
        }
        std::vector<Json> desc(roots.size());
        std::vector<std::vector<std::vector<GRPoint>>> pins(roots.size());
        for(size_t i=0;i<roots.size();++i) {
            auto& c=desc[i];
            int d=roots[i]->dieIdx;
            // 独立命名使同一逻辑网在单 Die 的不同连通分量不会被详细布线器短接。
            c["name"]="NC3D_"+std::to_string(net.id)+"_"+std::to_string(i);
            c["net_id"]=net.id;c["die"]=d;
            c["local_net"]=net.localNet[d]<0?std::string():g.model.dies[d]->design->getAllNets().at(net.localNet[d]).getName();
            c["pins"]=Json::array();c["landings"]=Json::array();
        }
        size_t ordinal=0;
        for(int d=0;d<2;++d) for(const auto& pin:net.pins) if(pin.die==d && !pin.mappingPort) {
            const auto& ap=g.terminals(net.id).at(ordinal++);
            auto hit=std::find_if(ap.begin(),ap.end(),[&](GRPoint p){return group.count(p);});
            if(hit==ap.end()) throw std::logic_error("Unreachable output terminal");
            const auto& die=*g.model.dies[d]->design;
            auto i=group.at(*hit);
            pins[i].push_back({*hit});
            desc[i]["pins"].push_back({pin.original.pinIndex<0?"PIN":die.instance(pin.original.instanceIndex).getName(),die.pinName(pin.original)});
        }
        for(const auto& edge:tree.edges) if(edge.type==GREdgeType::HBT) {
            const auto& s=g.sites.at(edge.site); usedSites.insert(s.id);++assignments;
            hbt<<net.id<<'\t'<<s.id<<'\t'<<s.bonded[0]<<'\t'<<s.bonded[1];
            for(auto p:{edge.a,edge.b}) {
                int d=p.dieIdx,i=group.at(p);
                Json landing;
                landing["id"]=s.id;landing["name"]="NC3DH_"+std::to_string(net.id)+"_"+std::to_string(s.id);
                landing["xy"]=s.local[d];landing["layer"]=g.model.dies[d]->grid.getLayerName(p.layerIdx);
                desc[i]["landings"].push_back(landing);pins[i].push_back({p});
                hbt<<'\t'<<s.local[d][0]<<'\t'<<s.local[d][1]<<'\t'<<landing["layer"].get<std::string>()<<'\t'<<desc[i]["name"].get<std::string>();
            }
            hbt<<'\t'<<(s.owner>=0)<<'\n';
        }
        for(size_t i=0;i<roots.size();++i) {
            const auto name=desc[i]["name"].get<std::string>();
            int d=roots[i]->dieIdx;
            const auto& grid=g.model.dies[d]->grid;
            GRNet local(net.id,name,pins[i],roots[i]);
            std::vector<std::pair<int,utils::BoxT<int>>> guides;
            GlobalRouter::componentGuides(grid,g.parameters(),local,guides,pinArea,wireArea);
            for(const auto& pin:pins[i]) for(auto p:pin) guides.emplace_back(p.layerIdx,utils::BoxT<int>(p.x,p.y));
            std::set<std::array<DBU,5>> unique;
            for(const auto& box:guides) unique.insert({{grid.getGridline(0,box.second.x.low),
                grid.getGridline(1,box.second.y.low),grid.getGridline(0,box.second.x.high+1),
                grid.getGridline(1,box.second.y.high+1),box.first}});
            auto& os=d?bottom:top;
            os<<name<<"\n(\n";
            for(const auto& b:unique) os<<b[0]<<' '<<b[1]<<' '<<b[2]<<' '<<b[3]<<' '<<grid.getLayerName(b[4])<<'\n';
            os<<")\n";components<<desc[i].dump()<<'\n';
        }
    }
    top.close();bottom.close();hbt.close();components.close();treeLog.close();
    Json out;
    out["wire_um_by_die"]={length[0],length[1]};out["wirelength_um"]=length[0]+length[1];
    out["via_count"]=vias;out["used_hbt"]=usedSites.size();out["hbt_assignments"]=assignments;
    double overflow[3]={};
    for(const auto& r:g.demand()) overflow[static_cast<int>(std::get<0>(r.first))]+=std::max(0.,r.second-g.capacity(r.first));
    out["wire_overflow"]=overflow[0];out["via_capacity_overflow"]=overflow[1];out["hbt_capacity_overflow"]=overflow[2];
    out["pin_patch_area"]=pinArea;out["wire_patch_area"]=wireArea;
    return out;
}
}
