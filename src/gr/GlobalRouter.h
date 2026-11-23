#pragma once
#include "global.h"
#include "obj/Design.h"
#include "GridGraph.h"
#include "GRNet.h"

class GlobalRouter {
public:
    GlobalRouter(const Design& design, const Parameters& params, bool buildNetlist = true);
    void route();
    void write(std::string guide_file = "");
    void writeNetGuide(std::ostream& out, const GRNet& net, const GRPoint* landing = nullptr,
                       bool includeIsolatedRoot = false);
    void writeNetGuides(std::ostream& out, const std::string& name,
                        const vector<GRNet>& components, const vector<GRPoint>& landings);
    GridGraph& getGridGraph() { return gridGraph; }
    
private:
    const Parameters& parameters;
    GridGraph gridGraph;
    vector<GRNet> nets;
    
    int areaOfPinPatches = 0;
    int areaOfWirePatches = 0;
    
    void sortNetIndices(vector<int>& netIndices) const;
    void getGuides(const GRNet& net, vector<std::pair<int, utils::BoxT<int>>>& guides) ;
    void writeGuideBoxes(std::ostream& out, const std::string& name,
                         const vector<std::pair<int, utils::BoxT<int>>>& guides) const;
    void printStatistics() const;
};
