// 异构网格中的离散站点集合。四叉树用于区域枚举和八方向最近站点查询。
// 同一类也可表示普通 routing 网格（不占用），以统一位置 DP 的候选生成。
#pragma once
#include "H3DTree.h"
#include <array>

namespace h3d {

class HbtGrid {
public:
    HbtGrid(std::vector<DBU> xs, std::vector<DBU> ys, const std::vector<int>& blocked = {});
    utils::PointT<DBU> point(int site) const;
    int siteAt(utils::PointT<DBU> position) const;
    int owner(int site) const { return owners.at(site); }
    int available() const { return quads.front().available; }
    int size() const { return owners.size(); }
    const utils::BoxT<DBU>& bounds() const { return quads.front().bounds; }
    const std::vector<DBU>& xCoordinates() const { return xs; }
    const std::vector<DBU>& yCoordinates() const { return ys; }
    std::vector<int> within(const utils::BoxT<DBU>& box) const;
    int nearest(const utils::BoxT<DBU>& box, int dx, int dy) const;
    // 原文未规定最近点平局：论文模式保留全部同距离最小点，避免任意丢弃合法最近候选。
    std::vector<int> nearestAll(const utils::BoxT<DBU>& box, int dx, int dy) const;
    std::vector<int> candidates(const utils::BoxT<DBU>& box) const;
    // 整网原子提交；同网重复站点只占一次，不同网或 blocked 站点不能共享。
    void occupy(const std::vector<int>& sites, int net);
    void release(const std::vector<int>& sites, int net);

private:
    struct Quad {
        int xl, xh, yl, yh;
        int parent;
        int available = 0;
        std::array<int, 4> children = {{-1, -1, -1, -1}};
        utils::BoxT<DBU> bounds;
    };
    std::vector<DBU> xs, ys;
    std::vector<int> owners; // -1 空闲，-2 禁用，其他为原始线网 ID。
    std::vector<int> leaves;
    std::vector<Quad> quads;
    int build(int xl, int xh, int yl, int yh, int parent);
    void change(int site, int delta);
};

} // namespace h3d
