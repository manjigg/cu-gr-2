// 文件功能：声明全局布线总调度器，管理共享网格资源、全体线网、统计和 guide 输出。
// route 执行三阶段优化；write 将离散布线树转换成详细布线器使用的物理矩形。
//
// 【白话职责】这是总调度员：它安排每条线网什么时候布、哪些要重布以及最后怎样输出。
// gridGraph 是所有线网共用的资源账本；nets 是需要逐个处理的网格线网列表。
// PatternRoute/MazeRoute 像专门处理一条线网的工作人员，不负责整个芯片调度。
// route() 的结果先写到各 GRNet 的 routingTree 中；write() 再生成磁盘文件。
// getGuides 为单条线网生成区域；printStatistics 汇总全体线网的线长/过孔/溢出。
// write 的默认参数 "" 表示调用者没给新路径时使用 Parameters.out_file。
#pragma once
#include "global.h"
#include "obj/Design.h"
#include "GridGraph.h"
#include "GRNet.h"

class GlobalRouter {
public:
    GlobalRouter(const Design& design, const Parameters& params);
    void route();
    void write(std::string guide_file = "");
    
private:
    // parameters 只读算法设置，gridGraph 本调度器拥有的全芯片资源账本。
    const Parameters& parameters;
    GridGraph gridGraph;
    // nets[netIndex]：当前信号网对象，待办列表只存这些编号而不复制对象。
    vector<GRNet> nets;
    
    // areaOfPinPatches/areaOfWirePatches 是补丁“格子×层”累计数量，不是 DBU²。
    // write 开始置 0，当前累加不做重叠去重，因此也不是区域并集面积。
    int areaOfPinPatches;
    int areaOfWirePatches;
    
    // netIndices 是输入输出待办编号清单，按网格包围盒半周长原地重排。
    void sortNetIndices(vector<int>& netIndices) const;
    // guides[i].first 是真实层编号；second 是二维闭区间格子盒，方法向列表追加。
    void getGuides(const GRNet& net, vector<std::pair<int, utils::BoxT<int>>>& guides) ;
    void printStatistics() const;
};
