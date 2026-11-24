#pragma once
#include "global.h"
#include "obj/Design.h"
#include "GridGraph.h"
#include "GRNet.h"

namespace native3d { class MultiDieDesign; }

class GlobalRouter {
public:
    GlobalRouter(const Design& design, const Parameters& params);
    void route();
#ifdef CUGR_NATIVE_3D
    static int route(native3d::MultiDieDesign&,const std::string& output);
#endif
    static void componentGuides(const GridGraph&,const Parameters&,const GRNet&,
        vector<std::pair<int,utils::BoxT<int>>>&,int& pinArea,int& wireArea);
    void write(std::string guide_file = "");
    
private:
    const Parameters& parameters;
    GridGraph gridGraph;
    vector<GRNet> nets;
    
    int areaOfPinPatches;
    int areaOfWirePatches;
    
    void sortNetIndices(vector<int>& netIndices) const;
    void getGuides(const GRNet& net, vector<std::pair<int, utils::BoxT<int>>>& guides) ;
    void printStatistics() const;
};