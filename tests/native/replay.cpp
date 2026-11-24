#include "native/RoutingGraph.h"
#include <cmath>

namespace {
using namespace native3d;

struct Record {
    int net;
    Edge edge;
    double length;
};

bool nextRecord(std::istream& stream, Record& row) {
    std::string line;
    if(!std::getline(stream,line)) {
        if(!stream.eof()) throw std::runtime_error("Cannot read routed edge table");
        return false;
    }
    std::istringstream input(line);
    std::string type, trailing;
    int site,da,la,xa,ya,db,lb,xb,yb;
    if(!(input>>row.net>>type>>site>>da>>la>>xa>>ya>>db>>lb>>xb>>yb>>row.length)
        || input>>trailing || !std::isfinite(row.length))
        throw std::invalid_argument("Malformed routed edge: "+line);
    static const std::map<std::string,GREdgeType> kinds{{"WIRE",GREdgeType::WIRE},{"VIA",GREdgeType::VIA},{"HBT",GREdgeType::HBT}};
    row.edge=Edge({da,la,xa,ya},{db,lb,xb,yb},kinds.at(type),site);
    return true;
}

GRPoint singletonAccess(const std::vector<std::vector<GRPoint>>& pins) {
    if(!pins.empty()) for(auto p:pins.front())
        if(std::all_of(pins.begin(),pins.end(),[&](const std::vector<GRPoint>& ap) {
            return std::find(ap.begin(),ap.end(),p)!=ap.end();
        })) return p;
    throw std::invalid_argument("Physical terminals cannot connect without an edge");
}

Json replay(const MultiDieDesign& model,std::istream& input) {
    RoutingGraph graph(model);
    std::string header;
    std::getline(input,header);
    if(header!="net_id\ttype\tsite_id\td0\tl0\tx0\ty0\td1\tl1\tx1\ty1\twire_um")
        throw std::invalid_argument("Unsupported routed edge table");
    Record row{-1,Edge({0,0,0},{0,0,0},GREdgeType::WIRE),0};
    bool more=nextRecord(input,row);
    uint64_t dbu[2]={},vias[2]={},hbt=0,nets=0,terms=0;
    for(const auto& n:model.nets) {
        Route tree{{0,0,0},{}};
        while(more && row.net==n.id) {
            if(!tree.edges.insert(row.edge).second) throw std::invalid_argument("Duplicate physical edge");
            // graph.commit 将检查首尾点、类型、硬容量及所有真实终端。
            if(!graph.validPoint(row.edge.a) || !graph.validPoint(row.edge.b))
                throw std::invalid_argument("Invalid physical endpoint");
            if(row.edge.type==GREdgeType::WIRE) {
                if(!graph.available(n.id,{row.edge})) throw std::invalid_argument("Unavailable wire");
                double physical=graph.wireUm(row.edge);
                if(std::abs(physical-row.length)>1e-9) throw std::invalid_argument("Wire length mismatch");
                const auto& grid=model.dies[row.edge.a.dieIdx]->grid;
                int dir=grid.getLayerDirection(row.edge.a.layerIdx);
                dbu[row.edge.a.dieIdx]+=grid.getEdgeLength(dir,row.edge.a[dir]);
            } else {
                if(row.length!=0) throw std::invalid_argument("Non-wire length mismatch");
                if(row.edge.type==GREdgeType::VIA) ++vias[row.edge.a.dieIdx];else ++hbt;
            }
            more=nextRecord(input,row);
        }
        tree.root=tree.edges.empty()?singletonAccess(graph.terminals(n.id)):tree.edges.begin()->a;
        try {graph.commit(n.id,tree);}
        catch(const std::exception& e) {throw std::runtime_error("Replaying net "+std::to_string(n.id)+": "+e.what());}
        ++nets;terms+=graph.terminals(n.id).size();
    }
    if(more) throw std::invalid_argument("Out-of-order or unknown net ID");
    double over[2]={},peak[2]={};
    uint64_t occupied=0,viaBad=0,hbtBad=0;
    for(const auto& item:graph.demand()) {
        double excess=std::max(0.,item.second-graph.capacity(item.first));
        auto type=std::get<0>(item.first);
        if(type==GREdgeType::WIRE) {
            int d=std::get<1>(item.first);
            over[d]+=excess;
            peak[d]=std::max(peak[d],excess);
        } else if(type==GREdgeType::VIA) viaBad+=excess>1e-12;
        else {++occupied;hbtBad+=excess>1e-12;}
    }
    if(occupied!=hbt) throw std::logic_error("HBT physical identity mismatch");
    auto entries=graph.demand().size();
    for(auto it=model.nets.rbegin();it!=model.nets.rend();++it) graph.release(it->id);
    if(!graph.demand().empty()) throw std::logic_error("Demand remains after reverse replay");
    Json report;
    report["nets"]=nets;report["physical_pins"]=terms;report["unconnected_nets"]=0;
    report["via_by_die"]={vias[0],vias[1]};report["hbt_count"]=occupied;
    report["via_capacity_violations"]=viaBad;report["hbt_violations"]=hbtBad;
    report["wire_overflow_by_die"]={over[0],over[1]};report["max_wire_overflow_by_die"]={peak[0],peak[1]};
    report["wire_dbu_by_die"]={dbu[0],dbu[1]};
    for(int d:{0,1}) {
        const auto& grid=model.dies[d]->grid;
        report["wire_um_by_die"].push_back(double(dbu[d])/grid.getLibDBU());
        report["wire_metric_by_die"].push_back(dbu[d]/grid.getM2Pitch());
    }
    report["resource_keys_after_commit"]=entries;report["resource_keys_after_release"]=graph.demand().size();
    return report;
}
}

int main(int argc,char** argv) try {
    if(argc!=4) throw std::invalid_argument("Usage: native_replay manifest.json route_edges.tsv new-report.json");
    if(std::ifstream(argv[3])) throw std::invalid_argument("Replay output already exists");
    Parameters params(argc,argv);
    native3d::MultiDieDesign model(params,argv[1]);
    std::ifstream input(argv[2]);
    if(!input) throw std::runtime_error("Cannot open routed edge table");
    auto result=replay(model,input);
    std::ofstream output;
    output.exceptions(std::ios::badbit|std::ios::failbit);
    output.open(argv[3]);output<<result.dump(2)<<'\n';output.close();
    return 0;
} catch(const std::exception& e) {
    std::cerr<<e.what()<<std::endl;
    return 1;
}
