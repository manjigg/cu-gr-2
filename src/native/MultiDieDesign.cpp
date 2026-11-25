#include "MultiDieDesign.h"
#include "Profile.h"
#include <boost/filesystem.hpp>

namespace native3d {
int integer(const Json& j) {
    if (!j.is_number_integer() || j < INT_MIN || j > INT_MAX)
        throw std::invalid_argument("Expected signed 32-bit integer: " + j.dump());
    return j.get<int>();
}
XY pairOf(const Json& j) {
    if (!j.is_array() || j.size()!=2) throw std::invalid_argument("Expected coordinate pair");
    return {{integer(j.at(0)), integer(j.at(1))}};
}
DieTransform::DieTransform(const Json& cfg,int packageUnits,int localUnits)
    : offset(pairOf(cfg.at("offset"))) {
    if (localUnits<=0 || packageUnits<=0 || packageUnits%localUnits)
        throw std::invalid_argument("Bonded DBU must be an integral multiple of local DBU");
    scale=packageUnits/localUnits;
    auto a=pairOf(cfg.at("axes")), s=pairOf(cfg.at("signs"));
    for (int i=0;i<2;++i) {
        if ((a[i]!=0 && a[i]!=1) || a[0]==a[1] || (s[i]!=1 && s[i]!=-1))
            throw std::invalid_argument("Invalid orthogonal die transform");
        axes[i]=a[i]; signs[i]=s[i];
    }
}
XY DieTransform::bonded(XY p) const {
    XY q;
    for (int i=0;i<2;++i) q[i]=offset[i]+signs[i]*scale*p[axes[i]];
    return q;
}
XY DieTransform::local(XY q) const {
    XY p;
    for (int i=0;i<2;++i) {
        auto delta=q[i]-offset[i];
        if (delta%scale) throw std::invalid_argument("HBT landing lies between local DBU");
        p[axes[i]]=signs[i]*(delta/scale);
    }
    return p;
}
static std::string resolve(const std::string& base,const Json& item) {
    return boost::filesystem::absolute(item.get<std::string>(),base).string();
}
static GridGraph initializeGrid(const Design& design,const Parameters& parameters) {
    Measured measured(Timing::Initialization);
    return GridGraph(design,parameters);
}
DieDesign::DieDesign(const Parameters& p,const Json& j,const std::string& base,int units)
    : design(Design::importDie(p,resolve(base,j.at("lef")),resolve(base,j.at("def")))),
      grid(initializeGrid(*design,p)),transform(j.at("transform"),units,design->getLibDBU()) {
    for (const auto& n:design->getAllNets())
        if (!names.emplace(n.getName(),n.getIndex()).second) throw std::invalid_argument("Duplicate local net");
}
MultiDieDesign::MultiDieDesign(const Parameters& p,const std::string& path):parameters(p) {
    std::ifstream stream(path);
    if (!stream) throw std::runtime_error("Cannot open design manifest " + path);
    stream >> config;
    units=integer(config.at("bonded_dbu"));
    const auto& inputs=config.at("dies");
    if (!inputs.is_array() || inputs.size()!=2) throw std::invalid_argument("Expected top and bottom design");
    const auto base=boost::filesystem::absolute(path).parent_path().string();
    for (int d=0;d<2;++d) dies[d].reset(new DieDesign(p,inputs.at(d),base,units));
    std::array<std::set<int>,2> used;
    auto attach=[&](GlobalNet& g,int d,int index,const std::string& cut) {
        if (!used[d].insert(index).second) throw std::invalid_argument("Local net mapped twice");
        g.localNet[d]=index;
        const auto& model=*dies[d]->design;
        const auto& pins=model.getAllNets().at(index).getAllPinRefs();
        int matches=0;
        for (int i=0;i<static_cast<int>(pins.size());++i) {
            bool witness=!cut.empty() && pins[i].pinIndex==-1 && model.pinName(pins[i])==cut;
            g.pins.push_back({d,index,i,pins[i],witness});
            matches+=witness;
        }
        if (!cut.empty() && (matches!=1 || pins.size()==1)) throw std::invalid_argument("Invalid cut port " + cut);
    };
    const auto& mapping=config.at("mapping");
    if (!mapping.is_array()) throw std::invalid_argument("Expected explicit net mapping array");
    for (const auto& m:mapping) {
        GlobalNet g; g.id=nets.size();
        for (int d=0;d<2;++d) {
            const auto& side=m.at(d?"bottom":"top");
            const auto name=side.at("net").get<std::string>();
            if (!dies[d]->names.count(name)) throw std::invalid_argument("Unknown mapped net " + name);
            attach(g,d,dies[d]->names.at(name),side.value("cut_port",std::string()));
        }
        nets.push_back(std::move(g));
    }
    for (int d=0;d<2;++d) for (const auto& n:dies[d]->design->getAllNets()) {
        if (used[d].count(n.getIndex())) continue;
        GlobalNet g; g.id=nets.size();
        attach(g,d,n.getIndex(),"");
        nets.push_back(std::move(g));
    }
}
Json MultiDieDesign::inventory() const {
    Json result;
    for (const auto& die:dies) {
        const auto& design=*die->design;
        Json side;
        side["local_nets"]=die->names.size();
        side["dbu"]=design.getLibDBU();
        side["grid"]={die->grid.getSize(0),die->grid.getSize(1),die->grid.getNumLayers()};
        side["via_layer_pairs"]=design.viaTypes().size();
        int64_t pins=0;
        for (const auto& net:design.getAllNets()) pins+=net.getAllPinRefs().size();
        side["pins"]=pins;
        result["dies"].push_back(side);
    }
    result["global_nets"]=nets.size();
    size_t mapped=0,real=0,witness=0;
    for (const auto& n:nets) {
        mapped+=n.localNet[0]>=0 && n.localNet[1]>=0;
        for (const auto& pin:n.pins) pin.mappingPort?++witness:++real;
    }
    result["cross_die_nets"]=mapped;
    result["real_pins"]=real;
    result["mapping_ports"]=witness;
    return result;
}
}
