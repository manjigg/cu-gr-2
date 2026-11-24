#pragma once
#include "gr/GRNet.h"
#include <array>
#include <map>

namespace native3d {
using Json = Rsyn::Json;
using XY = std::array<int64_t, 2>;

// 所有变换都在显式的 bonded DBU 下执行，支持轴交换、镜像及平移。
struct DieTransform {
    int64_t scale;
    std::array<int,2> axes, signs;
    XY offset;
    DieTransform(const Json& config, int packageUnits, int localUnits);
    XY bonded(XY local) const;
    XY local(XY package) const;
};
struct GlobalPin {
    int die, net, ordinal;
    PinReference original;
    bool mappingPort;
};
struct GlobalNet {
    int id;
    std::array<int,2> localNet{{-1,-1}};
    std::vector<GlobalPin> pins;
};
struct DieDesign {
    std::unique_ptr<Design> design;
    GridGraph grid;
    DieTransform transform;
    std::map<std::string,int> names;
    DieDesign(const Parameters& params, const Json& input, const std::string& base, int units);
};
class MultiDieDesign {
public:
    MultiDieDesign(const Parameters& params, const std::string& configPath);
    Json inventory() const;
    const Parameters& parameters;
    Json config;
    int units;
    std::array<std::unique_ptr<DieDesign>,2> dies;
    std::vector<GlobalNet> nets;
};
int integer(const Json& value);
XY pairOf(const Json& value);
}
