#pragma once
#include "obj/DesignInput.h"
#include "obj/Design.h"
#include "gr/GlobalRouter.h"
#include "H3DTree.h"
#include "HbtGrid.h"
#include <array>
#include <map>
#include <set>

namespace h3d {

struct OriginalNet {
    int id;
    std::string name;
    std::string use = "SIGNAL";
    std::array<std::vector<std::string>, 2> localNetNames;
    std::vector<PinReference> pins;
    std::vector<GRPoint> access;
    utils::BoxT<DBU> bounds;
    bool crossesDies() const;
};

struct DieData {
    std::unique_ptr<DesignInput> input;
    std::unique_ptr<Design> design;
    std::unique_ptr<GlobalRouter> router;
};

class H3DDesign {
public:
    explicit H3DDesign(const Parameters& parameters);
    H3DTree planarTree(const OriginalNet& net) const;
    // Optimization may use the uniform-grid representative of an HBT. DEF
    // geometry must always retain the actual legal bonding-site coordinates.
    utils::PointT<DBU> physicalHbtPoint(int site) const {
        return physicalHbtPoints.empty() ? hbt->point(site) : physicalHbtPoints.at(site);
    }
    Rsyn::Json config;
    std::array<DieData, 2> dies; // Bottom=0, Top=1。
    std::vector<OriginalNet> nets;
    std::unique_ptr<HbtGrid> hbt;
    std::unique_ptr<HbtGrid> routing;
    std::vector<utils::PointT<DBU>> physicalHbtPoints;
    std::array<int, 2> landingLayers;
    utils::PointT<DBU> padSize;
    std::array<std::map<int, ::PinReference>, 2> hbtPorts;
    std::array<std::map<int, std::string>, 2> hbtPortNames;
    std::array<std::set<std::string>, 2> reservedNetNames;
    std::array<std::set<std::string>, 2> reservedPortNames;
};

utils::PointT<DBU> cellCenter(const GridGraph& graph, utils::PointT<int> cell);
utils::PointT<int> cellAt(const GridGraph& graph, utils::PointT<DBU> point);

} // namespace h3d
