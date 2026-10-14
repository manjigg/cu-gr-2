// 文件功能：定义指定朝向的单元模板 Macro，以及实际放置的单元/端口 Instance。
// Macro 保存局部引脚和障碍；Instance 保存模板引用及位置，端口直接保存物理矩形。
//
// 【白话职责】Macro 像一张零件图，Instance 像放在芯片上的一个实际零件。
// 一百个相同朝向的同型号零件，可以引用同一个 Macro，再各自存位置。
// pins 是“每个引脚 -> 多个形状”的二维列表；obstacles 是不能随意穿过的金属区域。
// Instance 的 type=CELL 表示普通单元，type=PORT 表示芯片对外连接的端口。
// CELL 用 macroIndex 和 position；PORT 用直接保存的 port 矩形。
// 查询前先 isCell()，避免把端口当成拥有模板编号的普通单元。
#pragma once
#include "global.h"
#include "GeoTypes.h"

class Macro {
    // Macros of a SPECIFIC orientation (NOT the original macro)
public:
    Macro(int idx, const Rsyn::Cell& cell, const Rsyn::PhysicalDesign& physicalDesign);
    int getPinIndex(std::string pinName);
    const vector<BoxOnLayer>& getObstacles() const { return obstacles; }
    const vector<BoxOnLayer>& getPinShapes(int pinIndex) const { return pins[pinIndex]; }
    
private:
    // name：库模板名字；orientation：旋转/镜像姿态，不是 H/V 布线方向。
    std::string name;
    int orientation;
    // index：预留模板编号字段；当前构造虽接收 idx，却没有给该字段赋值。
    // 因而不能从参数传入就推断它已初始化，实际访问主要靠模板列表下标。
    int index;
    // 字典将引脚名（如 A/B/Y）换成 pins 列表下标，避免路由时反复查名字。
    robin_hood::unordered_map<std::string, int> pinIndices;
    // pins[pinIndex][shapeIndex]：某信号引脚的第 shapeIndex 块局部带层矩形。
    vector<vector<BoxOnLayer>> pins;
    // obstacles[i]：第 i 块局部占用矩形，包括引脚金属和库 OBS。
    vector<BoxOnLayer> obstacles;
};

class Instance {
public:
    const static int CELL = 0;
    const static int PORT = 1;
    
    // idx 实例编号，_name 实例名字，macroIndex 模板编号，pos 放置 DBU 坐标。
    Instance(int idx, std::string _name, int macroIndex, utils::PointT<DBU> pos):
        index(idx), name(_name), macroIndex(macroIndex), position(pos), type(CELL) {}
    Instance(int idx, std::string _name, const Rsyn::PhysicalPort& phyPort);
    bool isCell() const { return type == CELL; }
    int getMacroIndex() const { return macroIndex; }
    utils::PointT<DBU> getPosition() const { return position; }
    const BoxOnLayer& getPort() const { return port; }
    
private:
    // name 是此实例名如 U1，index 是 Design.instances 下标，type 区分 CELL/PORT。
    std::string name;
    int index;
    int type;
    // For cells
    // position 是普通单元 DBU 放置位置，模板形状加此位移成为全局坐标。
    utils::PointT<DBU> position;
    // macroIndex 指向 Design.macros 的型号+朝向模板。
    int macroIndex;
    // for ports
    // port 是外部端口已完成变换的全局带层矩形，普通 CELL 不用此字段。
    BoxOnLayer port;
};
