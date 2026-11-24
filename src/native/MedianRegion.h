#pragma once
#include "MultiDieDesign.h"
namespace native3d {
struct MedianRegion { XY low,high; };
// 加权 L1 目标可按两个坐标轴分离，所有连续最优点为该闭矩形。
MedianRegion weightedMedianRegion(const std::vector<XY>& points,const std::vector<unsigned>& weights);
}
