// 文件功能：通过 Rsyn 导入 LEF/DEF，转换为 CUGR 数据对象，并计算单位布线代价。
// 同时处理电源特殊线网障碍、网格线以及从单元局部坐标到芯片坐标的几何查询。
//
// 【输入和输出】输入是两个文件路径；输出不是新文件，而是填满 Design 的成员字段。
// 读取的顺序很重要：先有工艺层才能给引脚形状编号，先有实例才能建立引脚引用。
// read() 创建独立输入上下文；extract() 分五步提取层、实例、线网、障碍和网格。
// Rsyn 负责认识文件格式；这里负责把解析结果改装成路由器直接能用的数据。
// 例如库引脚局部矩形为 (0,0)-(10,5)，实例放在 (100,200)，查询后变成
// (100,200)-(110,205)。本例只是解释平移，实际还会先处理旋转/镜像。
// 函数参数里非 const 的 vector& 是“要装结果的列表”，方法可往里面追加内容。
#include "Design.h"
#include "DesignInput.h"

// 实现从输入文件中读取并整理这些数据

void Design::read(std::string lef_file, std::string def_file) {
    log() << "parsing..." << std::endl;
    loghline();
    // input 拥有本次导入的独立上下文，双 die 不经全局 Session 共享实例/线网。
    DesignInput input(lef_file, def_file);
    log() << "Rsyn finished loading lef/def" << std::endl;
    extract(input.physical, input.logical);
}

void Design::extract(Rsyn::PhysicalDesign physicalDesign, Rsyn::Design rsynDesign) {
    // 【本方法变量表】physicalDesign 物理数据库代理；rsynDesign 逻辑设计；rsynModule 顶层模块。
    // dieBound Rsyn 裸片边界；rsynLayer 当前工艺层；rsynTracks 当前层轨道集合。
    // numCells/numPorts/numSpecialNets 仅用于统计数量，不是对象下标。
    // instanceIndices：“实例名->实例下标”；macroNameIndices：“原型号名->编号”；
    // macroIndices：“原型号编号与朝向编码->CUGR 模板下标”。
    // rsynInstance 当前实例，cell/port 为按类型转换后的逻辑代理，phyLibCell/phyPort 物理代理。
    // macroName 原型号名，macroNameIndex 原型号编号，orientation 朝向，macroHash 组合键；
    // macroIndex 新模板的列表下标，instanceIndex 新实例下标，position DBU 放置坐标。
    // rsynNet 当前输入网，net 刚加入 CUGR 列表的网引用；rsynPin 当前端点；p 字典查找位置。
    // pinIndex 模板中的引脚下标，macro 该实例所属模板引用。
    // specialNet/wire/segment 当前特殊网/路径/段；layerIdx 段当前所在真实层；width 线宽 DBU。
    // pos/nextPos 上一/当前路径点 DBU 坐标；ext/nextExt 各点线端延伸 DBU；i 路径点序号。
    // dim 几何轴 0=x/1=y；l/h 线段沿该轴的物理低/高边界；box/botBox/topBox 占用矩形。
    // via 当前过孔；botLayerIdx/topLayerIdx 上下金属层；geo 当前形状；bounds 平移后的边界。
    // numRowCol 生成式过孔列/行数；origin 过孔原点；botOff/topOff 上下金属偏移原点；
    // length 切孔阵列半跨度；botEnc/topEnc 加包围后的金属半跨度，均用 DBU。
    // dimension 网格轴；low/high 裸片该轴 DBU 边界；rsynGCell 一组粗网格定义；
    // location 起点、step 步长均为 DBU；numStep 重复数；direction 网格线几何方向。
    // 执行顺序按下面 5 个编号段：前段创建的模板/实例是后段建立引脚引用的前提。
    // Design::extract 的 :: 表示“这是 Design 类型的方法”，不是新建另一个类。
    Rsyn::Module rsynModule = rsynDesign.getTopModule();
    
    libDBU = physicalDesign.getDatabaseUnits(Rsyn::LIBRARY_DBU);
    auto dieBound = physicalDesign.getPhysicalDie().getBounds();
    dieRegion = getBoxFromRsynBounds(dieBound);
    
    
    // 1. Get layers
    // 范围 for：依次取出列表里的每个层。const & 是只读借用当前层。
    std::size_t routingIndex = 0;
    for (const Rsyn::PhysicalLayer& rsynLayer : physicalDesign.allPhysicalLayers()) {
        if (rsynLayer.getType() == Rsyn::ROUTING) {
            if (!routingLayers.empty()) {
                if (routingIndex >= routingLayers.size()) { routingIndex++; continue; }
                if (routingLayers[routingIndex] != rsynLayer.getName()) {
                    throw std::invalid_argument("Requested routing layers must be a contiguous prefix of the LEF metal stack");
                }
            }
            routingIndex++;
            const auto& rsynTracks = physicalDesign.allPhysicalTracks(rsynLayer);
            // emplace_back 在列表末尾直接构造一个 MetalLayer，转换规则及轨道信息。
            layers.emplace_back(rsynLayer, rsynTracks, libDBU);
        } else if (rsynLayer.getType() == Rsyn::CUT) {
        
        }
    }
    
    if (!routingLayers.empty() && layers.size() != routingLayers.size()) {
        throw std::invalid_argument("Requested routing layer was not found in LEF");
    }

    // 2. Get macros and instances
    int numCells = 0;
    int numPorts = 0;
    // unordered_map 可理解为字典：“实例名称 -> 列表中的编号”。
    // 比起每次从头扫描所有实例，按名字查字典通常更快。
    robin_hood::unordered_map<std::string, int> instanceIndices;
    robin_hood::unordered_map<std::string, int> macroNameIndices; // Original macros
    // 同名模板在不同朝向下拥有不同引脚几何，因此缓存键包含模板名索引和朝向。
    robin_hood::unordered_map<uint64_t, int> macroIndices; // Macros with a specific orientation
    for (const Rsyn::Instance& rsynInstance : rsynModule.allInstances()) {
        if (rsynInstance.getType() == Rsyn::CELL) {
            numCells++;
            const Rsyn::Cell& cell = rsynInstance.asCell();
            const Rsyn::PhysicalLibraryCell& phyLibCell = physicalDesign.getPhysicalLibraryCell(cell);
            const std::string& macroName = phyLibCell.getMacro()->name();
            if (macroNameIndices.find(macroName) == macroNameIndices.end()) {
                macroNameIndices.emplace(macroName, macroNameIndices.size());
            }
            int macroNameIndex = macroNameIndices[macroName];
            int orientation = physicalDesign.getPhysicalCell(cell).getTransform().getOrientation();
            // 把“原模板编号”和“朝向编号”编码成一把钥匙。
            // 同名模板朝向相同才能共用几何；不同朝向需要不同变换结果。
            uint64_t macroHash = macroNameIndex * Rsyn::NUM_PHY_ORIENTATION + orientation;
            
            const DBUxy position = rsynInstance.getPosition();
            // find 找键，end 表示没找到；只在第一次遇到这个模板/朝向组合时提取几何。
            if (macroIndices.find(macroHash) == macroIndices.end()) {
                int macroIndex = macros.size();
                macros.emplace_back(macroIndex, cell, physicalDesign);
                macroIndices.emplace(macroHash, macroIndex);
            }
            
            int instanceIndex = instances.size();
            instanceIndices[rsynInstance.getName()] = instanceIndex;
            instances.emplace_back(
                instanceIndex, rsynInstance.getName(), macroIndices[macroHash], utils::PointT<DBU>(position.x, position.y)
            );
        } else if (rsynInstance.getType() == Rsyn::PORT) {
            numPorts++;
            const Rsyn::Port& port = rsynInstance.asPort();
            const Rsyn::PhysicalPort& phyPort = physicalDesign.getPhysicalPort(port);
            
            int instanceIndex = instances.size();
            instanceIndices[rsynInstance.getName()] = instanceIndex;
            instances.emplace_back(instanceIndex, rsynInstance.getName(), phyPort);
        }
    }
    
    // 只导入需要连接的信号线网；端口以 pinIndex=-1 区别于单元内部引脚。
    // 3. Get Nets
    for (const Rsyn::Net rsynNet : rsynModule.allNets()) {
        // || 是“或者”；continue 跳过这一轮，继续看下一个线网。
        // 电源/地的固定几何后面当障碍处理，不作为普通信号线网重新选路径。
        if (rsynNet.getUse() == Rsyn::POWER || rsynNet.getUse() == Rsyn::GROUND) continue;
        nets.emplace_back(nets.size(), rsynNet.getName());
        // auto 让编译器从右边推断类型；& 借用列表最后一个对象，后续添加引脚会改到它。
        auto& net = nets.back();
        for (const auto& rsynPin : rsynNet.allPins()) {
            if (rsynPin.isPort()) {
                auto p = instanceIndices.find(rsynPin.getName());
                if (p == instanceIndices.end()) {
                    log() << "Error: cannot find instance " << rsynPin.getName() << std::endl;
                } else {
                    net.addPinRef(p->second, -1);
                }
            } else {
                auto p = instanceIndices.find(rsynPin.getInstance().getName());
                if (p == instanceIndices.end()) {
                    log() << "Error: cannot find instance " << rsynPin.getInstance().getName() << std::endl;
                }
                int instanceIndex = p->second;
                Macro& macro = macros[instances[instanceIndex].getMacroIndex()];
                auto pinIndex = macro.getPinIndex(rsynPin.getName());
                net.addPinRef(instanceIndex, pinIndex);
            }
        }
    }
    
    // 特殊线网（如电源）已占用的线段和过孔金属形状用于扣减网格容量。
    // 4. Get special nets as obstacles
    int numSpecialNets = 0;
    for (Rsyn::PhysicalSpecialNet specialNet : physicalDesign.allPhysicalSpecialNets()) {
        numSpecialNets++;
        for (const DefWireDscp& wire : specialNet.getNet().clsWires) {
            for (const DefWireSegmentDscp& segment : wire.clsWireSegments) {
                int layerIdx = physicalDesign.getPhysicalLayerByName(segment.clsLayerName).getRelativeIndex();
                const DBU width = segment.clsRoutedWidth;
                DBUxy pos;
                DBU ext = 0;
                for (unsigned i = 0; i != segment.clsRoutingPoints.size(); ++i) {
                    const DefRoutingPointDscp& pt = segment.clsRoutingPoints[i];
                    const DBUxy& nextPos = pt.clsPos;
                    const DBU nextExt = pt.clsHasExtension ? pt.clsExtension : 0;
                    if (i >= 1) {
                        for (unsigned dim = 0; dim != 2; ++dim) {
                            if (pos[dim] == nextPos[dim]) continue;
                            const DBU l = pos[dim] < nextPos[dim] ? pos[dim] - ext : nextPos[dim] - nextExt;
                            const DBU h = pos[dim] < nextPos[dim] ? nextPos[dim] + nextExt : pos[dim] + ext;
                            BoxOnLayer box(layerIdx);
                            // 线段沿 dim 方向延伸，另一方向按线宽向两边扩张，得到占用矩形。
                            // 延伸量 ext/nextExt 是端点之外额外的金属，不可只画两点间的细线。
                            box[dim].Set(l, h);
                            box[1 - dim].Set(pos[1 - dim] - width / 2, pos[1 - dim] + width / 2);
                            obstacles.emplace_back(box);
                            break;
                        }
                    }
                    pos = nextPos;
                    ext = nextExt;
                    // ! 是“取反”。没有过孔则继续读取下一个路径点。
                    // 有过孔时要把上下层金属形状都记成障碍，并更新后续路径所在层。
                    if (!pt.clsHasVia) continue;
                    const Rsyn::PhysicalVia& via = physicalDesign.getPhysicalViaByName(pt.clsViaName);
                    const int botLayerIdx = via.getBottomLayer().getRelativeIndex();
                    for (const Rsyn::PhysicalViaGeometry& geo : via.allBottomGeometries()) {
                        Bounds bounds = geo.getBounds();
                        bounds.translate(pos);
                        const BoxOnLayer box(botLayerIdx, getBoxFromRsynBounds(bounds));
                        obstacles.emplace_back(box);
                    }
                    const int topLayerIdx = via.getTopLayer().getRelativeIndex();
                    for (const Rsyn::PhysicalViaGeometry& geo : via.allTopGeometries()) {
                        Bounds bounds = geo.getBounds();
                        bounds.translate(pos);
                        const BoxOnLayer box(topLayerIdx, getBoxFromRsynBounds(bounds));
                        obstacles.emplace_back(box);
                    }
                    if (via.hasViaRule()) {
                        const utils::PointT<int> numRowCol =
                            via.hasRowCol() ? utils::PointT<int>(via.getNumCols(), via.getNumRows())
                                            : utils::PointT<int>(1, 1);
                        BoxOnLayer botBox(botLayerIdx);
                        BoxOnLayer topBox(topLayerIdx);
                        for (unsigned dimIdx = 0; dimIdx != 2; ++dimIdx) {
                            const Dimension dim = static_cast<Dimension>(dimIdx);
                            const DBU origin = via.hasOrigin() ? pos[dim] + via.getOrigin(dim) : pos[dim];
                            const DBU botOff =
                                via.hasOffset() ? origin + via.getOffset(Rsyn::BOTTOM_VIA_LEVEL, dim) : origin;
                            const DBU topOff =
                                via.hasOffset() ? origin + via.getOffset(Rsyn::TOP_VIA_LEVEL, dim) : origin;
                            const DBU length =
                                (via.getCutSize(dim) * numRowCol[dim] + via.getSpacing(dim) * (numRowCol[dim] - 1)) / 2;
                            const DBU botEnc = length + via.getEnclosure(Rsyn::BOTTOM_VIA_LEVEL, dim);
                            const DBU topEnc = length + via.getEnclosure(Rsyn::TOP_VIA_LEVEL, dim);
                            botBox[dim].Set(botOff - botEnc, botOff + botEnc);
                            topBox[dim].Set(topOff - topEnc, topOff + topEnc);
                        }
                        obstacles.emplace_back(botBox);
                        obstacles.emplace_back(topBox);
                    }
                    if (layerIdx == botLayerIdx)
                        layerIdx = topLayerIdx;
                    else if (layerIdx == topLayerIdx)
                        layerIdx = botLayerIdx;
                    else {
                        log() << "Error: via " << pt.clsViaName << "of special net " << specialNet.getNet().clsName 
                              << " is on a wrong layer " << layerIdx << std::endl;
                        break;
                    }
                }
            }
        }
    }
    
    // 优先使用 DEF 的 GCELLGRID；缺失时以 defaultGridlineSpacing 生成网格边界。
    // 5. Get gridlines
    // resize(2) 给 x/y 各建一份边界列表；网格数是边界数量减 1。
    // 例如边界 [0,100,200] 划出两个格子，不是三个格子。
    gridlines.resize(2);
    if (physicalDesign.allPhysicalGCell().empty()) {
        for (unsigned dimension = 0; dimension < 2; dimension++) {
             const int low = dieRegion[dimension].low;
             const int high = dieRegion[dimension].high;
             for (DBU i = low; i < high; i += gridlineSpacing) {
                 gridlines[dimension].push_back(i);
             }
             if (gridlines[dimension].back() != high)
                 gridlines[dimension].push_back(high);
         }
    } else {
        for (const Rsyn::PhysicalGCell &rsynGCell : physicalDesign.allPhysicalGCell()) {
            int location = rsynGCell.getLocation();
            int step = rsynGCell.getStep();
            int numStep = rsynGCell.getNumTracks();
            unsigned direction = (rsynGCell.getDirection() == Rsyn::PhysicalGCellDirection::VERTICAL ? MetalLayer::V : MetalLayer::H);
            for (int i = 1; i < numStep; i++) gridlines[1 - direction].push_back(location + step * i);
        }
        for (unsigned dimension = 0; dimension < 2; dimension++) {
            gridlines[dimension].push_back(dieRegion[dimension].low);
            sort(gridlines[dimension].begin(), gridlines[dimension].end());
            gridlines[dimension].erase(std::unique(gridlines[dimension].begin(), gridlines[dimension].end()), gridlines[dimension].end());
            if (gridlines[dimension].back() != dieRegion[dimension].high)
                gridlines[dimension].push_back(dieRegion[dimension].high);
        }
    }
    log() << "Finished reading lef/def" << std::endl;
    logmem();
    logeol();
    
    log() << "design statistics" << std::endl;
    loghline();
    log() << "lib DBU:             " << libDBU << std::endl;
    log() << "die region (in DBU): " << dieRegion << std::endl;
    log() << "num of cells:        " << numCells << std::endl;
    log() << "num of ports:        " << numPorts << std::endl;
    log() << "num of nets :        " << nets.size() << std::endl;
    log() << "num of special nets: " << numSpecialNets << std::endl;
    log() << "gcell grid:          " << gridlines[0].size() - 1 << " x " << gridlines[1].size() - 1 << " x " << getNumLayers() << std::endl;
    logeol();
    
}

// 用 M2 轨距归一化线长和短路面积；面积代价乘线宽后转成每 DBU 长度的代价。
void Design::setUnitCosts() {
    // m2_pitch 是归一化用的 M2 轨距 DBU；unit_area_short_cost 是每 DBU² 短路评分。
    // layerIndex 逐层下标，乘该层线宽把面积分转换成每 DBU 长度分。
    // 若 M2 轨距为 100 DBU，线长权重为 0.5，则每 DBU 线长评分是 0.005。
    // 除以轨距把不同设计尺度统一成“若干轨距长的线”；数字只用于示例。
    if (layers.size() < 3 || layers[1].getPitch() <= 0) {
        throw std::runtime_error("Routing requires at least three metal layers and valid M2 tracks");
    }
    DBU m2_pitch = layers[1].getPitch();
    unit_length_wire_cost = parameters.weight_wire_length / m2_pitch;
    unit_via_cost = parameters.weight_via_number;
    unit_length_short_costs.resize(layers.size());
    // 面积要除以长度尺度的平方。再乘各层线宽，得到每单位线长对应的面积代价。
    const CostT unit_area_short_cost = parameters.weight_short_area / (m2_pitch * m2_pitch);
    for (int layerIndex = 0; layerIndex < layers.size(); layerIndex++) {
        unit_length_short_costs[layerIndex] = unit_area_short_cost * layers[layerIndex].getWidth();
    } 
}

std::pair<std::string, std::string> Design::getPinNames(const PinReference& pin) const {
    const auto& instance = instances.at(pin.instanceIndex);
    return {instance.getName(), instance.isCell() ? macros.at(instance.getMacroIndex()).getPinName(pin.pinIndex) : ""};
}

PinReference Design::addPort(const std::string& name, const BoxOnLayer& shape) {
    int index = instances.size();
    instances.emplace_back(index, name, shape);
    return {index, -1};
}

// 模板已完成朝向变换，只需平移到实例位置；端口的几何已经是芯片坐标。
void Design::getPinShapes(const PinReference& pinRef, vector<BoxOnLayer>& pinShapes) const {
    // instance 是 pinRef 指向的实际实例；macro 是它使用的朝向模板；position 是实例位移。
    // macroPins 为该信号引脚局部矩形列表；macroPin 是当前矩形；pinShapes 是输出列表。
    // CELL：拷贝局部矩形后平移；PORT：直接追加实例已存的全局端口矩形。
    // 输入是“哪个实例的哪个引脚”，输出追加到 pinShapes。
    // 同一个引脚可以有多个矩形，不应认为一个引脚一定只有一个点。
    const auto& instance = instances[pinRef.instanceIndex];
    if (instance.isCell()) {
        const Macro& macro = macros[instance.getMacroIndex()];
        const utils::PointT<DBU> position = instance.getPosition();
        const vector<BoxOnLayer>& macroPins = macro.getPinShapes(pinRef.pinIndex);
        for (const auto& macroPin : macroPins) {
            pinShapes.push_back(macroPin);
            // back() 取刚加入的矩形；ShiftBy 给 x/y 上下界都加实例放置位移。
            pinShapes.back().ShiftBy(position);
        }
    } else {
        pinShapes.push_back(instance.getPort());
    }
}

// 按层汇总单元和特殊线网障碍；默认跳过 M1，与 GridGraph 容量初始化对应。
void Design::getAllObstacles(vector<vector<utils::BoxT<DBU>>>& allObstacles, bool skipM1) const {
    // allObstacles[layer][i] 输出第 layer 层第 i 个矩形；skipM1 是否跳过层 0。
    // instance 当前单元，position 位移，macro 模板，macroObstacles 其局部障碍，obs 当前障碍。
    // 前一循环把模板障碍平移，后一循环追加设计级全局障碍；两组坐标来源不同。
    // 输出按层分组：allObstacles[layer] 是这一层的所有占用区域。
    // skipM1=true 时忽略索引 0 的层；层编号与通常的 M1/M2 名称差一位。
    allObstacles.resize(getNumLayers());
    // cell obstacles
    for (const auto& instance : instances) {
        if (!instance.isCell()) {
            const auto& port = instance.getPort();
            if (port.layerIdx > 0 || !skipM1) allObstacles[port.layerIdx].emplace_back(port.x, port.y);
            continue;
        }
        utils::PointT<DBU> position = instance.getPosition();
        const auto& macro = macros[instance.getMacroIndex()];
        const vector<BoxOnLayer>& macroObstacles = macro.getObstacles();
        for (const BoxOnLayer& obs : macroObstacles) {
            if (obs.layerIdx >= getNumLayers()) continue;
            if (obs.layerIdx > 0 || !skipM1) {
                allObstacles[obs.layerIdx].emplace_back(obs.x, obs.y);
                allObstacles[obs.layerIdx].back().ShiftBy(position);
            }
        }
    }
    // other obstacles
    for (const BoxOnLayer& obs : obstacles) {
        if (obs.layerIdx >= getNumLayers()) continue;
        if (obs.layerIdx > 0 || !skipM1) {
            allObstacles[obs.layerIdx].emplace_back(obs.x, obs.y);
        }
    }
}
