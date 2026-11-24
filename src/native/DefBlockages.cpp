#include "DefBlockages.h"
#include "def5.8/defrReader.hpp"
#include <cstdio>

namespace native3d {
namespace {
struct Blockages {
    const Design& die;
    std::vector<BoxOnLayer> shapes;
    std::exception_ptr error;
    int defUnits=0;
};
int units(defrCallbackType_e,double scale,defiUserData data) {
    auto& b=*static_cast<Blockages*>(data);
    if(scale<1 || scale!=std::floor(scale) || scale>INT_MAX) return 1;
    b.defUnits=static_cast<int>(scale);return 0;
}
int geometry(defrCallbackType_e,defiBlockage* shape,defiUserData data) {
    if(!shape->hasLayer()) return 0;
    auto& b=*static_cast<Blockages*>(data);
    try {
        if(shape->hasSlots()||shape->hasFills()) return 0;
        if(shape->hasMask()||shape->hasDesignRuleWidth()||shape->hasPushdown())
            throw std::invalid_argument("Unsupported mask, rule width or pushdown routing blockage");
        if(!b.defUnits) throw std::invalid_argument("Missing DEF blockages units");
        int layer=0;
        while(layer<b.die.getNumLayers() && b.die.getLayer(layer).getName()!=shape->layerName()) ++layer;
        if(layer==b.die.getNumLayers()) throw std::invalid_argument("Unsupported routing blockage layer");
        auto coord=[&](int64_t value) {
            auto scaled=value*b.die.getLibDBU();
            if(scaled%b.defUnits) throw std::invalid_argument("Sub-DBU blockage");
            return DBU(scaled/b.defUnits);
        };
        auto rect=[&](int x0,int y0,int x1,int y1) {
            int64_t padding=shape->hasSpacing()?shape->minSpacing():0;
            b.shapes.emplace_back(layer,coord(x0-padding),coord(y0-padding),coord(x1+padding),coord(y1+padding));
        };
        for(int i=0;i<shape->numRectangles();++i) rect(shape->xl(i),shape->yl(i),shape->xh(i),shape->yh(i));
        for(int i=0;i<shape->numPolygons();++i) {
            // 正交多边形按相邻 x 坐标间的水平截面分解为无重叠矩形。
            auto polygon=shape->getPolygon(i);
            std::set<int> xs(polygon.x,polygon.x+polygon.numPoints);
            for(auto a=xs.begin();a!=xs.end() && std::next(a)!=xs.end();++a) {
                int x0=*a,x1=*std::next(a);
                std::vector<int> ys;
                for(int k=0;k<polygon.numPoints;++k) {
                    int j=(k+1)%polygon.numPoints;
                    if(polygon.x[k]==polygon.x[j]) continue;
                    if(polygon.y[k]!=polygon.y[j]) throw std::invalid_argument("Non-Manhattan blockage");
                    if(std::min(polygon.x[k],polygon.x[j])<=x0 && std::max(polygon.x[k],polygon.x[j])>=x1)
                        ys.push_back(polygon.y[k]);
                }
                if(ys.size()%2) throw std::invalid_argument("Malformed blockage polygon");
                std::sort(ys.begin(),ys.end());
                for(size_t k=0;k<ys.size();k+=2) rect(x0,ys[k],x1,ys[k+1]);
            }
        }
    } catch(...) {b.error=std::current_exception();return 1;}
    return 0;
}
}
std::vector<BoxOnLayer> routingBlockages(const Design& die,const std::string& path) {
    Blockages b{die,{},nullptr};
    std::ifstream stream(path);
    std::string line;
    bool found=false;
    while(std::getline(stream,line)) if(line.find("BLOCKAGES")!=std::string::npos) {found=true;break;}
    if(!found) return {};
    std::unique_ptr<FILE,decltype(&fclose)> f(fopen(path.c_str(),"r"),fclose);
    if(!f) throw std::runtime_error("Cannot open DEF for blockage import");
    defrInit();defrReset();defrUnsetCallbacks();
    defrSetUnitsCbk(units);defrSetBlockageCbk(geometry);
    int status=defrRead(f.get(),path.c_str(),&b,1);
    defrReleaseNResetMemory();defrClear();
    if(b.error) std::rethrow_exception(b.error);
    if(status) throw std::invalid_argument("Invalid DEF blockage section");
    return b.shapes;
}
}
