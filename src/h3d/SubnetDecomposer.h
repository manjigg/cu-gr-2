#pragma once
#include "H3DDesign.h"
#include "HbtLocationOptimizer.h"
#include "gr/PatternRoute.h"
#include "SubnetPartition.h"

namespace h3d {

struct Subnet {
    int original;
    int die;
    int routingIndex = -1;
    std::string name;
    std::string use;
    std::vector<::PinReference> pins;
    std::vector<GRPoint> terminals;
    std::vector<int> hbtSites;
    std::shared_ptr<SteinerTreeNode> topology;
};

class SubnetDecomposer {
public:
    // 在每个 HBT 处拆成两个 die 的终端；同网同站点先合并，再按连通分量生成子网。
    std::vector<Subnet> run(const OriginalNet& net, const AssignedTopology& topology,
                            const LocationResult& locations, H3DDesign& design) const;
    std::vector<Subnet> runPaper(const OriginalNet& net, const AssignedTopology& topology,
                                 const LocationResult& locations, const SubnetPartition& partition,
                                 H3DDesign& design) const;
};

} // namespace h3d
