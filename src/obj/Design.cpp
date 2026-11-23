#include "Design.h"
#include "rsyn/io/parser/lef_def/DEFControlParser.h"
#include "rsyn/io/parser/lef_def/LEFControlParser.h"
#include "rsyn/io/reader/PopulateRsyn.h"

namespace {
struct DieNetlistReader : Rsyn::PopulateRsyn {
    using Rsyn::PopulateRsyn::populateRsyn;
};
}

Design::Design(const Parameters& params, const std::string& lef, const std::string& def)
    : parameters(params) {
    if (!std::ifstream(lef) || !std::ifstream(def)) {
        throw std::runtime_error("Cannot open die input: " + lef + ", " + def);
    }
    LefDscp library;
    DefDscp layout;
    LEFControlParser().parseLEF(lef, library);
    DEFControlParser().parseDEF(def, layout);
    if (!layout.clsDatabaseUnits || library.clsLefUnitsDscp.clsDatabase != layout.clsDatabaseUnits) {
        throw std::runtime_error("F2F import requires matching LEF and DEF DBU within each die: " + def);
    }
    for (const auto& port : layout.clsPorts) portNetNames.emplace(port.clsName, port.clsNetName);
    Rsyn::Design logical(def);
    DieNetlistReader().populateRsyn(library, layout, logical);
    Rsyn::PhysicalDesign physical(nullptr);
    physical.initPhysicalDesign(logical, {{"clsEnableMergeRectangles", false},
                                         {"clsEnableNetPinBoundaries", true}});
    physical.loadLibrary(library);
    physical.loadDesign(layout);
    extract(logical, physical);
    setUnitCosts();
    legalVias.assign(layers.size() - 1, false);
    auto addVia = [&](int a, int b) {
        if (a >= 0 && b >= 0 && std::max(a, b) < layers.size() && std::abs(a - b) == 1) {
            legalVias[std::min(a, b)] = true;
        }
    };
    for (const auto& via : physical.allPhysicalVias()) {
        const auto lower = via.getBottomLayer();
        const auto upper = via.getTopLayer();
        if (lower && upper && lower.getType() == Rsyn::ROUTING && upper.getType() == Rsyn::ROUTING) {
            addVia(lower.getRelativeIndex(), upper.getRelativeIndex());
        }
    }
    for (const auto& rule : library.clsLefViaRuleDscps) {
        if (!rule.clsIsGenerate) continue;
        vector<int> routing;
        for (const auto& layer : rule.clsLayers) {
            const auto physicalLayer = physical.getPhysicalLayerByName(layer.clsName);
            if (physicalLayer && physicalLayer.getType() == Rsyn::ROUTING) {
                routing.push_back(physicalLayer.getRelativeIndex());
            }
        }
        if (routing.size() == 2) addVia(routing[0], routing[1]);
    }
}

void Design::read(std::string lef_file, std::string def_file) {
    log() << "parsing..." << std::endl;
    loghline();
    const Rsyn::Json params = {{"lefFile", lef_file}, {"defFile", def_file}};
    Rsyn::ISPD2018Reader reader;
    reader.load(params);
    log() << "Rsyn finished loading lef/def" << std::endl;
    // GET DATA FOR DESIGN 
    
    Rsyn::Session session;
    Rsyn::PhysicalService* physicalService = session.getService("rsyn.physical");
    Rsyn::PhysicalDesign physicalDesign= physicalService->getPhysicalDesign();
    Rsyn::Design rsynDesign = session.getDesign();
    extract(rsynDesign, physicalDesign);
}

void Design::extract(Rsyn::Design rsynDesign, Rsyn::PhysicalDesign physicalDesign) {
    Rsyn::Module rsynModule = rsynDesign.getTopModule();
    
    libDBU = physicalDesign.getDatabaseUnits(Rsyn::LIBRARY_DBU);
    auto dieBound = physicalDesign.getPhysicalDie().getBounds();
    dieRegion = getBoxFromRsynBounds(dieBound);
    
    
    // 1. Get layers
    for (const Rsyn::PhysicalLayer& rsynLayer : physicalDesign.allPhysicalLayers()) {
        if (rsynLayer.getType() == Rsyn::ROUTING) {
            const auto& rsynTracks = physicalDesign.allPhysicalTracks(rsynLayer);
            layers.emplace_back(rsynLayer, rsynTracks, libDBU);
        } else if (rsynLayer.getType() == Rsyn::CUT) {
        
        }
    }
    
    // 2. Get macros and instances
    int numCells = 0;
    int numPorts = 0;
    robin_hood::unordered_map<std::string, int> instanceIndices;
    robin_hood::unordered_map<std::string, int> macroNameIndices; // Original macros
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
            uint64_t macroHash = macroNameIndex * Rsyn::NUM_PHY_ORIENTATION + orientation;
            
            const DBUxy position = rsynInstance.getPosition();
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
    
    // 3. Get Nets
    for (const Rsyn::Net rsynNet : rsynModule.allNets()) {
        if (rsynNet.getUse() == Rsyn::POWER || rsynNet.getUse() == Rsyn::GROUND) continue;
        nets.emplace_back(nets.size(), rsynNet.getName());
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
                    throw std::runtime_error("Cannot find instance " + rsynPin.getInstance().getName());
                }
                int instanceIndex = p->second;
                Macro& macro = macros[instances[instanceIndex].getMacroIndex()];
                auto pinIndex = macro.getPinIndex(rsynPin.getName());
                if (pinIndex < 0) throw std::runtime_error("Cannot resolve pin " + rsynPin.getName());
                net.addPinRef(instanceIndex, pinIndex);
            }
        }
    }
    
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
                            box[dim].Set(l, h);
                            box[1 - dim].Set(pos[1 - dim] - width / 2, pos[1 - dim] + width / 2);
                            obstacles.emplace_back(box);
                            break;
                        }
                    }
                    pos = nextPos;
                    ext = nextExt;
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
    
    // 5. Get gridlines
    gridlines.resize(2);
    if (physicalDesign.allPhysicalGCell().empty()) {
        for (unsigned dimension = 0; dimension < 2; dimension++) {
             const DBU low = dieRegion[dimension].low;
             const DBU high = dieRegion[dimension].high;
             gridlines[dimension].push_back(low);
             for (DBU i = low + defaultGridlineSpacing; i + defaultGridlineSpacing < high; i += defaultGridlineSpacing) {
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

void Design::setUnitCosts() {
    if (layers.size() < 2 || layers[1].getPitch() <= 0) {
        throw std::runtime_error("Design needs at least two routing layers and a valid second-layer track pitch");
    }
    DBU m2_pitch = layers[1].getPitch();
    unit_length_wire_cost = parameters.weight_wire_length / m2_pitch;
    unit_via_cost = parameters.weight_via_number;
    unit_length_short_costs.resize(layers.size());
    const CostT unit_area_short_cost = parameters.weight_short_area / (m2_pitch * m2_pitch);
    for (int layerIndex = 0; layerIndex < layers.size(); layerIndex++) {
        unit_length_short_costs[layerIndex] = unit_area_short_cost * layers[layerIndex].getWidth();
    } 
}

void Design::getPinShapes(const PinReference& pinRef, vector<BoxOnLayer>& pinShapes) const {
    const auto& instance = instances[pinRef.instanceIndex];
    if (instance.isCell()) {
        const Macro& macro = macros[instance.getMacroIndex()];
        const utils::PointT<DBU> position = instance.getPosition();
        const vector<BoxOnLayer>& macroPins = macro.getPinShapes(pinRef.pinIndex);
        for (const auto& macroPin : macroPins) {
            pinShapes.push_back(macroPin);
            pinShapes.back().ShiftBy(position);
        }
    } else {
        pinShapes.push_back(instance.getPort());
    }
}

void Design::getAllObstacles(vector<vector<utils::BoxT<DBU>>>& allObstacles, bool skipM1) const {
    allObstacles.resize(getNumLayers());
    // cell obstacles
    for (const auto& instance : instances) {
        if (!instance.isCell()) continue;
        utils::PointT<DBU> position = instance.getPosition();
        const auto& macro = macros[instance.getMacroIndex()];
        const vector<BoxOnLayer>& macroObstacles = macro.getObstacles();
        for (const BoxOnLayer& obs : macroObstacles) {
            if (obs.layerIdx > 0 || !skipM1) {
                allObstacles[obs.layerIdx].emplace_back(obs.x, obs.y);
                allObstacles[obs.layerIdx].back().ShiftBy(position);
            }
        }
    }
    // other obstacles
    for (const BoxOnLayer& obs : obstacles) {
        if (obs.layerIdx > 0 || !skipM1) {
            allObstacles[obs.layerIdx].emplace_back(obs.x, obs.y);
        }
    }
}
