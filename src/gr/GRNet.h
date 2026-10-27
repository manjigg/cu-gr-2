// 文件功能：定义全局布线线网 GRNet，保存引脚候选接入点、网格包围盒和最终三维布线树。
// pinAccessPoints 按引脚分组；routingTree 由 PatternRoute 计算并替换。
//
// 【白话职责】Net 是引脚名单，GRNet 是把这份名单放到网格地图上的版本。
// 一个引脚可能很大，覆盖多个格子，所以它有多个候选接入点，并非只有一个。
// pinAccessPoints[引脚编号][候选编号] 给出候选的层/x/y；boundingBox 包住所有候选。
// routingTree 是当前最佳路线的根节点地址；刚初始化时尚未计算出路线。
// shared_ptr 表示共享管理对象的指针，最后一个拥有者释放后自动清理内存。
// setRoutingTree 替换内存中的结果，资源图的加减则由 GlobalRouter 另外调用。
// 因而“换了树”与“提交了资源需求”是两件需要配合完成的事。
#pragma once
#include "global.h"
#include "obj/Design.h"
#include "GridGraph.h"

class GRNet {
public:
    GRNet(const Net& baseNet, const Design& design, const GridGraph& gridGraph);
    GRNet(int idx, const std::string& netName, vector<vector<GRPoint>> accessPoints);
    
    int getIndex() const { return index; }
    std::string getName() const { return name; }
    int getNumPins() const { return pinAccessPoints.size(); }
    const vector<vector<GRPoint>>& getPinAccessPoints() const { return pinAccessPoints; }
    const utils::BoxT<int>& getBoundingBox() const { return boundingBox; }
    const std::shared_ptr<GRTreeNode>& getRoutingTree() const { return routingTree; }
    // void getGuides(vector<std::pair<int, utils::BoxT<int>>>& guides) const;
    
    void setRoutingTree(std::shared_ptr<GRTreeNode> tree) { routingTree = tree; }
    void clearRoutingTree() { routingTree = nullptr; }
    
private:
    // index 是与 baseNet 相同的信号网编号；name 是输入线网文字名。
    int index;
    std::string name;
    // pinAccessPoints[pin][candidate]：该引脚的第 candidate 个真实层/x/y 网格位置。
    vector<vector<GRPoint>> pinAccessPoints;
    // boundingBox：所有候选的二维索引包围盒，不存层范围，不是最终导线并集。
    utils::BoxT<int> boundingBox;
    // routingTree：当前三维结果树根；刚开始为空，替换它不自动登记资源需求。
    std::shared_ptr<GRTreeNode> routingTree;
};
