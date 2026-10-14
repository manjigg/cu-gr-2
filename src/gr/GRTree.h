// 文件功能：定义三维网格点 GRPoint 和以 children 连接的全局布线树 GRTreeNode。
// 同层父子边表示直线导线，异层父子边表示同一网格位置的过孔堆叠。
//
// 【白话职责】用“点和它的孩子”保存路线，而不是保存一张图片。
// GRPoint 是三维地址 (layerIdx,x,y)，其中 x/y 是格子编号。
// GRTreeNode 除了这个地址，还保存 children，即沿路线向下连接的节点。
// 假设父节点 (1,2,3)，孩子 (1,5,3)，意味着索引 1 层上从 x=2 走到 x=5。
// 如果孩子是 (3,2,3)，则位置不变而层改变，表示逐层换到索引 3 的层。
// : public 表示继承，GRTreeNode 自动拥有 GRPoint 的层坐标和二维点功能。
// 树只有一个根，但每个节点可有多个孩子，这样能表达多引脚线网的分支。
#pragma once
#include "global.h"

class GRPoint: public utils::PointT<int> {
public:
    // int x
    // int y
    // layerIdx 在结果树为真实层下标，在 SparseGraph 临时为 H/V；x/y 是格子索引。
    int layerIdx;
    GRPoint(int l, int _x, int _y): layerIdx(l), utils::PointT<int>(_x, _y) {}
    friend inline std::ostream& operator<<(std::ostream& os, const GRPoint& pt) {
        // operator<< 是自定义打印规则，使 log()<<point 能输出三元坐标。
        // 返回流的引用 os，才能继续接着写 << 其他内容。
        os << "(" << pt.layerIdx << ", " << pt.x << ", " << pt.y << ")";
        return os;
    }
};

class GRTreeNode: public GRPoint {
public:
    // children[i] 与本节点以导线或过孔堆叠相接的第 i 个孩子，共享指针管理寿命。
    vector<std::shared_ptr<GRTreeNode>> children;
    
    GRTreeNode(int l, int _x, int _y): GRPoint(l, _x, _y) {}
    GRTreeNode(const GRPoint& point): GRPoint(point) {}
    // ~GRTreeNode() { for (auto& child : children) child->~GRTreeNode(); }
    static void preorder(std::shared_ptr<GRTreeNode> node, std::function<void(std::shared_ptr<GRTreeNode>)> visit);
    static void print(std::shared_ptr<GRTreeNode> node);
};
