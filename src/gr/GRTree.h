#pragma once
#include "global.h"

enum class GREdgeType { WIRE, VIA, HBT };

class GRPoint: public utils::PointT<int> {
public:
    // int x
    // int y
    int dieIdx = 0;
    int layerIdx;
    GRPoint(int d, int l, int col, int row): utils::PointT<int>(col,row), dieIdx(d), layerIdx(l) {}
    auto key() const { return std::make_tuple(dieIdx,layerIdx,x,y); }
    bool operator==(const GRPoint& p) const { return key()==p.key(); }
    bool operator!=(const GRPoint& p) const { return !(*this==p); }
    bool operator<(const GRPoint& p) const { return key()<p.key(); }
    GRPoint(int l, int _x, int _y): layerIdx(l), utils::PointT<int>(_x, _y) {}
    friend inline std::ostream& operator<<(std::ostream& os, const GRPoint& pt) {
        os << "(" << pt.layerIdx << ", " << pt.x << ", " << pt.y << ")";
        return os;
    }
};

class GRTreeNode: public GRPoint {
public:
    vector<std::shared_ptr<GRTreeNode>> children;
    // 在有根树中，每个非根节点唯一拥有一条入边。
    GREdgeType incoming = GREdgeType::WIRE;
    int siteId = -1;
    void attach(std::shared_ptr<GRTreeNode> child, GREdgeType type, int site = -1);
    
    GRTreeNode(int l, int _x, int _y): GRPoint(l, _x, _y) {}
    GRTreeNode(const GRPoint& point): GRPoint(point) {}
    // ~GRTreeNode() { for (auto& child : children) child->~GRTreeNode(); }
    static void preorder(std::shared_ptr<GRTreeNode> node, std::function<void(std::shared_ptr<GRTreeNode>)> visit);
    static void print(std::shared_ptr<GRTreeNode> node);
};