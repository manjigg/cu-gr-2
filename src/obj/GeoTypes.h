// 文件功能：定义带金属层索引的物理矩形 BoxOnLayer 和 Rsyn Bounds 转换接口。
// 坐标使用 DBU；与 GRPoint 的整数网格索引坐标区分。
//
// 【白话职责】普通矩形只说平面上占了哪里；BoxOnLayer 还说它位于哪层金属。
// 例如 M2 上 (100,200)-(120,240) 与 M3 上同坐标的矩形不是同一块金属。
// class BoxOnLayer : public BoxT<DBU> 是继承：保留矩形的 x/y 区间，再加层编号。
// template<typename... Args> 是可变参数模板：允许把不同形式的矩形参数转交基类。
// params... 表示展开所有参数，调用者无需分别写每一种矩形构造函数。
// friend 允许外部的打印运算符访问类内信息；这里用于把矩形写进日志。
#pragma once
#include "global.h"

utils::BoxT<DBU> getBoxFromRsynBounds(const Bounds& bounds);

class BoxOnLayer: public utils::BoxT<DBU> {
public:
    // layerIdx 真实布线层下标，-1 未指定；x/y 继承自矩形，为 DBU 区间。
    int layerIdx;

    //  constructors
    template <typename... Args>
    BoxOnLayer(int layerIndex = -1, Args... params) : layerIdx(layerIndex), utils::BoxT<DBU>(params...) {}

    // inherit setters from utils::BoxT in batch
    template <typename... Args>
    void Set(int layerIndex = -1, Args... params) {
        layerIdx = layerIndex;
        utils::BoxT<DBU>::Set(params...);
    }

    bool isConnected(const BoxOnLayer& rhs) const;

    friend ostream& operator<<(ostream& os, const BoxOnLayer& box);
};
