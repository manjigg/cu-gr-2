// 文件功能：实现 Rsyn 边界到 CUGR 矩形的转换、带层矩形的相交连通判断及打印。
// isConnected 是几何上的近似判定：同层或相邻层且矩形相交。
// 【白话流程】从 Rsyn Bounds 取左下角和右上角，填进 CUGR 的矩形类型。
// getBoxFromRsynBounds 返回 {lx,ly,hx,hy}；花括号是用这些值构造返回对象。
// isConnected 先看层差是否小于 2，再看平面形状是否相交，两个条件都满足才为真。
// && 表示“并且”；abs 求绝对值，例如 abs(2-3)=1。
// 这个判定是简化的几何条件，不会在这里检查是否真的放了合法的过孔。
#include "GeoTypes.h"

utils::BoxT<DBU> getBoxFromRsynBounds(const Bounds& bounds) {
    // bounds 输入 Rsyn 范围，getLower/getUpper 两角；返回顺序 lx/ly/hx/hy，单位保持 DBU。
    return {
        bounds.getLower().x, 
        bounds.getLower().y, 
        bounds.getUpper().x, 
        bounds.getUpper().y
    };
}

// BoxOnLayer

bool BoxOnLayer::isConnected(const BoxOnLayer& rhs) const {
    return abs(rhs.layerIdx - layerIdx) < 2 && HasIntersectWith(rhs);
}

ostream& operator<<(ostream& os, const BoxOnLayer& box) {
    os << "box(l=" << box.layerIdx << ", x=" << box[0] << ", y=" << box[1] << ")";
    return os;
}
