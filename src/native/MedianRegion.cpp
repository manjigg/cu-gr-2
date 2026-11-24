#include "MedianRegion.h"
namespace native3d {
MedianRegion weightedMedianRegion(const std::vector<XY>& points,const std::vector<unsigned>& weights) {
    if(points.empty()||points.size()!=weights.size()) throw std::invalid_argument("Invalid L1 terminals");
    uint64_t total=0;
    for(auto w:weights) {
        if(!w || UINT64_MAX-total<w) throw std::invalid_argument("Invalid L1 weight");
        total+=w;
    }
    MedianRegion region;
    for(int axis:{0,1}) {
        std::vector<std::pair<int64_t,unsigned>> sorted;
        for(size_t i=0;i<points.size();++i) sorted.emplace_back(points[i][axis],weights[i]);
        std::sort(sorted.begin(),sorted.end());
        uint64_t prefix=0;
        bool low=false;
        for(const auto& entry:sorted) {
            prefix+=entry.second;
            if(!low && prefix>=total-prefix) {region.low[axis]=entry.first;low=true;}
            if(prefix>total-prefix) {region.high[axis]=entry.first;break;}
        }
    }
    return region;
}
}
