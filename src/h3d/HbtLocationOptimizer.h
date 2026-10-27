#pragma once
#include "HbtGrid.h"
#include "DieAssignment.h"
#include <string>
#include <stdexcept>

namespace h3d {

class HbtResourceExhausted : public std::runtime_error {
public:
    HbtResourceExhausted(): std::runtime_error("HBT_RESOURCE_EXHAUSTED: no legal unoccupied HBT sites remain") {}
};

struct AssignedTopology {
    H3DTree tree;
    std::vector<Die> edgeDies;
    std::vector<int> hbtNodes;
    int root = -1;
};

// 将插在固定引脚处的 HBT 拆成可移动 HBT + 每 die 固定引脚节点。
AssignedTopology expandAssignment(const H3DTree& tree, const DieAssignmentResult& assignment);

struct PaperNodeAudit {
    int node;
    bool hbt;
    std::vector<int> children;
    utils::BoxT<DBU> region;
    std::vector<int> candidateSites;
    std::vector<DBU> costs;
    std::vector<std::vector<int>> childChoices; // [candidate][child slot]
    int selected = -1;
};

struct PaperTieMerge {
    std::vector<int> forcedNodes;
    int site;
    std::vector<int> selected; // 与 paperBfsOrder 对应，便于独立检查每次零代价合并。
};

struct LocationResult {
    std::vector<utils::PointT<DBU>> positions;
    std::vector<int> hbtSites; // 与 AssignedTopology.hbtNodes 对应；同网允许重复。
    DBU wirelength = 0;
    std::size_t candidateCount = 0;
    std::string mode = "exact_discrete_l1";
    int siteBudget = -1;
    bool budgetFallback = false;
    std::vector<int> paperRoots;
    std::vector<int> paperBfsOrder;
    std::vector<PaperNodeAudit> paperNodes;
    std::vector<utils::PointT<DBU>> continuousPositions;
    DBU continuousWirelength = 0;
    std::string paperTiePolicy = "first";
    std::size_t paperTieTrials = 0;
    int paperSitesBeforeTieBreak = 0;
    DBU paperWirelengthBeforeTieBreak = 0;
    std::size_t paperPairTieTrials = 0;
    std::vector<PaperTieMerge> paperTieMerges;
};

struct PaperAssignmentTrial {
    std::uint32_t seed;
    std::vector<Die> edgeDies;
    std::vector<int> hbtNodes;
    int physicalSites;
    DBU wirelength;
};

struct PaperAssignmentSolution {
    DieAssignmentResult assignment;
    AssignedTopology expanded;
    LocationResult location;
    std::vector<PaperAssignmentTrial> trials;
    int selectedTrial = 0;
};

class HbtLocationOptimizer {
    LocationResult runPaperImpl(const AssignedTopology& tree, const HbtGrid& hbt, const HbtGrid& routing,
                                std::uint32_t seed, bool coalesceTies, bool fullDomainAudit) const;
public:
    // III-C / Algorithm 1：连续中位区间 + Bv 内点 + 八方向全部最近点，候选集上的公式 (7)。
    LocationResult runPaper(const AssignedTopology& tree, const HbtGrid& hbt,
                             const HbtGrid& routing, std::uint32_t seed = 0, bool coalesceTies = true) const;
    // 仅用于单变量诊断：保留同一论文 BFS 森林、根和等成本规则，仅取消候选裁剪。
    // 不作为 paper 生产入口，不使用未来网站点预算。
    LocationResult runPaperFullDomainAudit(const AssignedTopology& tree, const HbtGrid& hbt,
                                          const HbtGrid& routing, std::uint32_t seed = 0) const;
    // 可选的数量 DP 等价回溯搜索：每次仍为原 mhc 最小值，并调用同一论文位置算法。
    // 先比较实际占用站点数，再比较各自位置最优值；不保证跨回溯家族的全局最优。
    PaperAssignmentSolution runPaperAssignments(const H3DTree& tree, const DieAssignmentResult& initial,
        const HbtGrid& hbt, const HbtGrid& routing, std::uint32_t seed, bool coalesceTies, int trialLimit) const;
    // 在全部可用 HBT / routing 点上精确求解公式 (7)。
    // L1 距离变换避免两两候选枚举；不在这里占用站点。
    LocationResult run(const AssignedTopology& tree, const HbtGrid& hbt, const HbtGrid& routing) const;
    // 精确求所有拓扑 HBT 共用一个站点时的最优位置和普通 Steiner 位置。
    LocationResult runSingleSite(const AssignedTopology& tree, const HbtGrid& hbt, const HbtGrid& routing) const;
    // 无约束最优方案满足预算时直接采用；否则采用精确单站点可行方案。
    // 对一般 K>1 不宣称基数约束问题全局最优。
    LocationResult runWithSiteBudget(const AssignedTopology& tree, const HbtGrid& hbt,
                                     const HbtGrid& routing, int maxSites) const;
};

} // namespace h3d
