// 文件功能：提取并变换单元引脚/障碍几何，建立引脚名索引，计算外部端口的物理形状。
// Macro 的旋转/镜像在本文件完成，实例位置的平移由 Design 查询时完成。
//
// 【白话流程】单元可能旋转或镜像，所以不能直接照搬库里的原始引脚坐标。
// 先取库单元原点和实例朝向，再把形状转换成“该朝向、相对实例位置”的局部坐标。
// 随后提取信号引脚名/形状和所有占用障碍；实例的具体位置留给 Design 再加。
// getPinIndex 输入引脚名字，输出编号；没找到时报告错误并返回 -1。
// 端口构造函数单独处理端口位置与朝向，得到直接可用于查询的芯片坐标。
#include "Instance.h"

Macro::Macro(int idx, const Rsyn::Cell& cell, const Rsyn::PhysicalDesign& physicalDesign) {
    // idx 传入模板编号（当前没有写入成员 index）；cell 是一个用于提取型号/朝向的实际单元。
    // libDBU 每微米 DBU；phyCell 实例物理资料；phyLibCell 库模板物理资料；transform 朝向变换。
    // origin 库原点的 DBU 位置；bounds 变换边界/当前形状边界，需根据所在代码段区分。
    // pin 当前逻辑引脚；pinIndex 信号引脚编号，-1 表示电源/地只作障碍；
    // phyLibPin 库引脚，phyPinGeo 第一形状组，phyPinLayer 当前层形状，layerIndex 真实层编号。
    // box 当前已按朝向变换的局部矩形；phyObs 当前库障碍，遍历其 allBounds 得到各矩形。
    const DBU libDBU = physicalDesign.getDatabaseUnits(Rsyn::LIBRARY_DBU);
    const Rsyn::PhysicalCell& phyCell = physicalDesign.getPhysicalCell(cell);
    const Rsyn::PhysicalLibraryCell& phyLibCell = physicalDesign.getPhysicalLibraryCell(cell);
    auto transform = phyCell.getTransform();
    
    name = phyLibCell.getMacro()->name();
    orientation = transform.getOrientation();
    
    // Macro origin
    const DBUxy origin(
        static_cast<DBU>(std::round(phyLibCell.getMacro()->originX() * libDBU)),
        static_cast<DBU>(std::round(phyLibCell.getMacro()->originY() * libDBU))
    );
    // 去掉实例放置位移，得到可供同朝向实例复用的局部坐标变换。
    // Shift transform bound to (0, 0)
    auto bounds = transform.getBounds();
    bounds.translate(-phyCell.getPosition());
    transform.setBounds(bounds);
    
    // Get pins 
    for (Rsyn::Pin pin : cell.allPins(false)) {
        // -1 是此处的“不是普通信号引脚”标记；不是有效 vector 下标。
        int pinIndex = -1;
        if (!pin.isPowerOrGround()) {
            pinIndex = pins.size();
            pinIndices.emplace(pin.getName(), pinIndex);
            pinNames.push_back(pin.getName());
            pins.emplace_back();
        }
        
        const Rsyn::PhysicalLibraryPin& phyLibPin = physicalDesign.getPhysicalLibraryPin(pin);
        // [0] 取第一组几何；本实现只读取这组，不是在循环全部几何组。
        const auto& phyPinGeo = phyLibPin.allPinGeometries()[0];
        for (const Rsyn::PhysicalPinLayer& phyPinLayer : phyPinGeo.allPinLayers()) {
            if (!phyPinLayer.hasRectangleBounds()) continue;
            int layerIndex = phyPinLayer.getLayer().getRelativeIndex();
            for (auto bounds : phyPinLayer.allBounds()) {
                // 先修正库原点，再按朝向旋转/镜像。顺序改变可能得到不同位置。
                bounds.translate(origin);
                bounds = transform.apply(bounds);
                BoxOnLayer box(layerIndex, bounds.getLower().x, bounds.getLower().y, bounds.getUpper().x, bounds.getUpper().y);
                if (pinIndex != -1) {
                    pins[pinIndex].push_back(box);
                }
                // 引脚金属也占用布线资源；电源/地引脚只进入障碍，不进入信号引脚表。
                obstacles.push_back(box);
            }
        }
    }
    
    // libObs
    for (const Rsyn::PhysicalObstacle& phyObs : phyLibCell.allObstacles()) {
        if (phyObs.getLayer().getType() != Rsyn::PhysicalLayerType::ROUTING) continue;
        const int layerIndex = phyObs.getLayer().getRelativeIndex();
        for (auto bounds : phyObs.allBounds()) {
            bounds.translate(origin);
            bounds = transform.apply(bounds);
            obstacles.emplace_back(layerIndex, bounds.getLower().x, bounds.getLower().y, bounds.getUpper().x, bounds.getUpper().y);
        }
    }
}

int Macro::getPinIndex(std::string pinName) {
    // pinName 要查的引脚文字名；p 是字典位置，p->first 名字、p->second 编号。
    // end 表示查不到，返回 -1；查到则返回用于 pins[编号] 的整数。
    auto p = pinIndices.find(pinName);
    if (p == pinIndices.end()) {
        log() << "Error: cannot find pin " << pinName << " in macro " << name << std::endl;
        return -1;
    } else {
        return p->second;
    }
}

// 对端口应用位置和朝向，保存可直接参与网格映射的芯片坐标矩形。
Instance::Instance(int idx, std::string _name, const Rsyn::PhysicalPort& phyPort): index(idx), name(_name), type(PORT) {
    if (!phyPort.getLayer()) throw std::invalid_argument("Missing physical layer for DEF port " + _name);
    // phyPort 输入端口资料，position DBU 位置，bounds 端口边界，transform 端口朝向变换。
    // 先把边界加到端口位置，再按此位置/朝向变换，结果写入成员 port。
    auto position = phyPort.getPosition();
    auto bounds = phyPort.getBounds();
    Rsyn::PhysicalTransform transform(Bounds(position, position), phyPort.getOrientation());
    bounds.translate(position);
    bounds = transform.apply(bounds);
    port = BoxOnLayer(phyPort.getLayer().getRelativeIndex(), getBoxFromRsynBounds(bounds));
}
