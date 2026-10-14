// 文件功能：定义金属层属性、首选布线方向、轨道位置和供容量/代价估计使用的规则。
// H=0 对应 x 方向，V=1 对应 y 方向；间距表按线宽和并行长度查询。
//
// 【白话职责】MetalLayer 是一层金属的“道路规则表”：通常往哪个方向走，
// 路宽多少、轨道隔多远、与旁边金属要留多大空隙、过孔附近需要多少金属面积。
// 轨道可看成导线中心可落下的平行线；firstTrackLoc、pitch、numTracks 决定其位置。
// 例：首轨 10、间距 20，则编号 0/1/2 的轨道在 10/30/50，单位都是 DBU。
// 本类只记录/查询当前全局模型用到的规则；最终制造规则仍由详细布线检查。
// vector<vector<DBU>> 是二维查表：先按线宽档，再按并行长度档，取所需间距。
#pragma once
#include "global.h"

class MetalLayer {
public:
    const static unsigned H = 0;
    const static unsigned V = 1;
    MetalLayer(const Rsyn::PhysicalLayer& rsynLayer, const vector<Rsyn::PhysicalTracks>& rsynTracks, const DBU libDBU);
    std::string getName() const { return name; }
    unsigned getDirection() const { return direction; }
    DBU getWidth() const { return width; }
    DBU getPitch() const { return pitch; }
    DBU getTrackLocation(const int trackIndex) const;
    utils::IntervalT<int> rangeSearchTracks(const utils::IntervalT<DBU>& locRange, bool includeBound = true) const;
    
    // Design rule related methods
    DBU getMinLength() const { return minLength; }
    DBU getDefaultSpacing() const { return defaultSpacing; }
    DBU getParallelSpacing(const DBU width, const DBU length = 0) const;
    DBU getMaxEolSpacing() const { return maxEolSpacing; }
    
private:
    // name 层文字名，index 相对层编号，direction 首选方向 H=0/V=1。
    std::string name;
    int index;
    unsigned direction;
    // width 默认线宽，minWidth 最小线宽；单位 DBU，均不是轨道数。
    DBU width;
    DBU minWidth;
    
    // tracks 
    // firstTrackLoc/lastTrackLoc 首/末轨道中心 DBU 坐标，沿走线方向的垂直轴测量。
    DBU firstTrackLoc;
    DBU lastTrackLoc;
    // pitch 相邻轨道间距 DBU，numTracks 轨道条数，下标为 0..numTracks-1。
    DBU pitch;
    int numTracks;
    
    // Design rules
    // Min area 
    // minArea 最小金属面积，虽类型为 DBU，物理单位为 DBU²。
    DBU minArea;
    // minLength 折算附加线长 max(minArea/width-width,0)，单位 DBU。
    DBU minLength;
    
    // Parallel run spacing 
    // parallelWidth[w] 宽度档、parallelLength[l] 并行长度档；都是 DBU。
    // parallelSpacing[w][l] 对应间距 DBU；w/l 是规则档下标，不是空间坐标。
    vector<DBU> parallelWidth = {0};
    vector<DBU> parallelLength = {0};
    vector<vector<DBU>> parallelSpacing = {{0}}; // width, length -> spacing
    // defaultSpacing：用默认线宽查询得到的间距摘要。
    DBU defaultSpacing = 0;
    
    // End-of-line spacing
    // EOL 线端规则：分别保存各条目最大间隙、适用端宽和作用范围，均为 DBU。
    // maxEol* 是保守摘要，并非完整的逐条件规则表。
    DBU maxEolSpacing = 0;
    DBU maxEolWidth = 0;
    DBU maxEolWithin = 0;
    
    // Corner spacing
    
};
