#include "H3DDesign.h"
#include "gr/PatternRoute.h"
#include <map>
#include <set>
#include <stdexcept>

namespace h3d {

bool OriginalNet::crossesDies() const {
    bool bottom = false, top = false;
    for (const auto& pin : pins) { bottom |= pin.die == Die::Bottom; top |= pin.die == Die::Top; }
    return bottom && top;
}

utils::PointT<DBU> cellCenter(const GridGraph& graph, utils::PointT<int> cell) {
    auto box = graph.getCellBox(cell);
    return {box.cx(), box.cy()};
}

utils::PointT<int> cellAt(const GridGraph& graph, utils::PointT<DBU> point) {
    utils::PointT<int> result;
    for (int axis = 0; axis < 2; axis++) {
        int low = 0, high = graph.getSize(axis);
        if (point[axis] < graph.getGridline(axis, 0) || point[axis] > graph.getGridline(axis, high)) {
            throw std::out_of_range("Topology position outside the die");
        }
        while (low < high) {
            int mid = (low + high) / 2;
            if (graph.getGridline(axis, mid + 1) <= point[axis]) low = mid + 1;
            else high = mid;
        }
        result[axis] = std::min(low, static_cast<int>(graph.getSize(axis)) - 1);
    }
    return result;
}

H3DDesign::H3DDesign(const Parameters& parameters) {
    std::ifstream configFile(parameters.hbt_config_file);
    if (!configFile) throw std::runtime_error("Cannot open HBT configuration");
    configFile >> config;
    if (config.value("schema_version", 1) != 1 || config.value("coordinate_frame", "bonded_xy") != "bonded_xy" ||
        config.value("top_transform", "identity") != "identity" || config.value("bottom_transform", "identity") != "identity") {
        throw std::invalid_argument("H3D input must use schema 1 and aligned bonded_xy coordinates (identity transforms)");
    }
    for (int die = 0; die < 2; die++) {
        std::string file = die == 0 ? parameters.bottom_def_file : parameters.top_def_file;
        dies[die].input.reset(new DesignInput(parameters.lef_file, file));
        auto layers = config.value("routing_layers", std::vector<std::string>());
        DBU gcell = config.value("gcell_pitch_dbu", static_cast<DBU>(0));
        if (gcell < 0) throw std::invalid_argument("GCell pitch must be positive when provided");
        dies[die].design.reset(new Design(parameters, dies[die].input->physical, dies[die].input->logical, layers, gcell));
        dies[die].router.reset(new GlobalRouter(*dies[die].design, parameters, false));
        for (const auto& net : dies[die].input->placement.clsNets) reservedNetNames[die].insert(net.clsName);
        for (const auto& port : dies[die].input->placement.clsPorts) reservedPortNames[die].insert(port.clsName);
    }
    const auto& bottom = *dies[0].design;
    const auto& top = *dies[1].design;
    if (bottom.getLibDBU() != top.getLibDBU() || bottom.getGridlines() != top.getGridlines() ||
        bottom.getDieRegion() != top.getDieRegion() ||
        config.at("dbu_per_micron").get<DBU>() != bottom.getLibDBU()) {
        throw std::invalid_argument("Both dies and HBT configuration need matching units, bounds and routing grids");
    }
    std::vector<DBU> mx, my;
    for (int axis = 0; axis < 2; axis++) {
        const auto& lines = bottom.getGridlines()[axis];
        auto& values = axis == 0 ? mx : my;
        for (std::size_t i = 1; i < lines.size(); i++) values.push_back((lines[i - 1] + lines[i]) / 2);
    }
    routing.reset(new HbtGrid(mx, my));
    // 诊断模式完整读取实际设计，但不猜测缺失的 HBT 物理 pad 尺寸。
    if (!parameters.h3d_init_only && !parameters.h3d_topology_only) {
    if (!config.count("pad_size_dbu") || config["pad_size_dbu"].is_null()) {
        throw std::invalid_argument("HBT pad_size_dbu is not supplied; physical HBT allocation requires explicit pad dimensions");
    }
    auto origin = config.at("origin_dbu").get<std::vector<DBU>>();
    auto pitch = config.at("pitch_dbu").get<std::vector<DBU>>();
    auto counts = config.at("site_counts").get<std::vector<int>>();
    auto pad = config.at("pad_size_dbu").get<std::vector<DBU>>();
    if (origin.size() != 2 || pitch.size() != 2 || counts.size() != 2 || pad.size() != 2) {
        throw std::invalid_argument("HBT coordinates, pitch, counts and pad size need two components");
    }
    if (counts[0] <= 0 || counts[1] <= 0 || static_cast<std::int64_t>(counts[0]) * counts[1] > std::numeric_limits<int>::max()) {
        throw std::invalid_argument("Invalid or oversized HBT array");
    }
    std::vector<DBU> hx, hy;
    for (int axis = 0; axis < 2; axis++) {
        if (counts[axis] <= 0 || pitch[axis] <= 0 || pad[axis] <= 0 || pad[axis] > pitch[axis]) {
            throw std::invalid_argument("Invalid HBT dimensions");
        }
        auto& values = axis == 0 ? hx : hy;
        for (int i = 0; i < counts[axis]; i++) {
            // 使用 long double 先验证整数计算范围，避免配置导致坐标乘法溢出。
            long double coordinate = static_cast<long double>(origin[axis]) + static_cast<long double>(i) * pitch[axis];
            if (coordinate < bottom.getDieRegion()[axis].low + pad[axis] / 2 ||
                coordinate > bottom.getDieRegion()[axis].high - (pad[axis] - pad[axis] / 2)) {
                throw std::invalid_argument("HBT pad outside the common die bounds");
            }
            values.push_back(origin[axis] + static_cast<DBU>(i) * pitch[axis]);
        }
    }
    padSize = {pad[0], pad[1]};
    std::set<int> blocked;
    if (config.count("blocked_sites")) for (const auto& site : config["blocked_sites"]) {
        auto ij = site.get<std::vector<int>>();
        if (ij.size() != 2 || ij[0] < 0 || ij[0] >= counts[0] || ij[1] < 0 || ij[1] >= counts[1]) {
            throw std::invalid_argument("Invalid blocked HBT site");
        }
        blocked.insert(ij[1] * counts[0] + ij[0]);
    }
    for (int die = 0; die < 2; die++) {
        std::string label = die == 0 ? "bottom" : "top";
        std::string layer = config.at("landing_layers").at(label).get<std::string>();
        landingLayers[die] = -1;
        for (int i = 0; i < dies[die].design->getNumLayers(); i++) {
            if (dies[die].design->getLayer(i).getName() == layer) landingLayers[die] = i;
        }
        if (landingLayers[die] != dies[die].design->getNumLayers() - 1) {
            throw std::invalid_argument("HBT landing layer must be the top-most routing layer of each die");
        }
        std::vector<std::vector<utils::BoxT<DBU>>> obstacles;
        dies[die].design->getAllObstacles(obstacles, false);
        RTree index;
        int id = 0;
        for (const auto& box : obstacles[landingLayers[die]]) {
            index.insert({boostBox(boostPoint(box.lx(), box.ly()), boostPoint(box.hx(), box.hy())), id++});
        }
        DBU spacing = dies[die].design->getLayer(landingLayers[die]).getDefaultSpacing();
        DBU hbtSpacing = config.value("spacing_dbu", static_cast<DBU>(0));
        if (hbtSpacing < 0) throw std::invalid_argument("HBT spacing must be nonnegative");
        for (int axis = 0; axis < 2; axis++) {
            if (static_cast<long double>(pitch[axis]) < static_cast<long double>(pad[axis]) + std::max(spacing, hbtSpacing)) {
                throw std::invalid_argument("HBT pitch is smaller than pad size plus required spacing");
            }
        }
        for (std::size_t y = 0; y < hy.size(); y++) for (std::size_t x = 0; x < hx.size(); x++) {
            boostBox padBox(boostPoint(hx[x] - pad[0] / 2 - spacing, hy[y] - pad[1] / 2 - spacing),
                            boostPoint(hx[x] + pad[0] - pad[0] / 2 + spacing, hy[y] + pad[1] - pad[1] / 2 + spacing));
            if (index.qbegin(bgi::intersects(padBox)) != index.qend()) blocked.insert(y * hx.size() + x);
        }
    }
    auto objective = config.value("hbt_optimization_coordinates", std::string("physical"));
    if (objective != "physical" && objective != "gcell_centers") {
        throw std::invalid_argument("hbt_optimization_coordinates must be physical or gcell_centers");
    }
    if (objective == "gcell_centers") {
        // The author's coarse grid is indexed on the uniform routing grid.
        // Keep physical sites separate so this representation change cannot
        // move emitted pads or silently change the available HBT resources.
        for (DBU y : hy) for (DBU x : hx) physicalHbtPoints.push_back({x, y});
        const auto& graph = dies[0].router->getGridGraph();
        for (int axis = 0; axis < 2; axis++) {
            auto& values = axis == 0 ? hx : hy;
            for (auto& coordinate : values) {
                utils::PointT<DBU> lower(mx.front(), my.front()), upper = lower;
                lower[axis] = coordinate - pad[axis] / 2;
                upper[axis] = coordinate + pad[axis] - pad[axis] / 2;
                int first = cellAt(graph, lower)[axis], last = cellAt(graph, upper)[axis];
                int best = first;
                DBU bestDistance = std::numeric_limits<DBU>::max();
                for (int i = first; i <= last; i++) {
                    DBU center = (graph.getGridline(axis, i) + graph.getGridline(axis, i + 1)) / 2;
                    DBU distance = std::abs(center - coordinate);
                    if (distance < bestDistance) { bestDistance = distance; best = i; }
                }
                coordinate = (graph.getGridline(axis, best) + graph.getGridline(axis, best + 1)) / 2;
            }
            if (std::adjacent_find(values.begin(), values.end(), std::greater_equal<DBU>()) != values.end()) {
                throw std::invalid_argument("HBT sites do not map injectively to the uniform grid");
            }
        }
    }
    hbt.reset(new HbtGrid(hx, hy, std::vector<int>(blocked.begin(), blocked.end())));
    }

    std::map<std::string, OriginalNet> grouped;
    for (int die = 0; die < 2; die++) {
        std::string label = die == 0 ? "bottom" : "top";
        std::map<std::string, std::string> uses;
        for (const auto& source : dies[die].input->placement.clsNets) {
            if (source.clsUse != INVALID_DEF_NAME) uses[source.clsName] = source.clsUse;
        }
        for (const auto& net : dies[die].design->getAllNets()) {
            std::string name = net.getName();
            if (config.count("net_aliases") && config["net_aliases"].count(label) && config["net_aliases"][label].count(name)) {
                name = config["net_aliases"][label][name].get<std::string>();
            }
            auto& target = grouped[name]; target.name = name;
            target.localNetNames[die].push_back(net.getName());
            auto use = uses.find(net.getName());
            if (use != uses.end()) target.use = use->second;
            for (const auto& pin : net.getAllPinRefs()) {
                target.pins.push_back({static_cast<Die>(die), pin.instanceIndex, pin.pinIndex});
                std::vector<BoxOnLayer> shapes;
                dies[die].design->getPinShapes(pin, shapes);
                for (const auto& shape : shapes) target.bounds = target.bounds.UnionWith(shape);
            }
        }
    }
    for (auto& entry : grouped) {
        auto& net = entry.second;
        if (net.pins.empty()) throw std::invalid_argument("Empty source net: " + net.name);
        for (const auto& pin : net.pins) {
            int die = static_cast<int>(pin.die);
            const auto& graph = dies[die].router->getGridGraph();
            std::vector<BoxOnLayer> shapes;
            dies[die].design->getPinShapes({pin.instanceIndex, pin.pinIndex}, shapes);
            int bestAccess = -1;
            DBU bestDistance = std::numeric_limits<DBU>::max();
            GRPoint best(-1, -1, -1);
            for (const auto& shape : shapes) {
                if (shape.layerIdx < 0 || shape.layerIdx >= graph.getNumLayers()) {
                    throw std::invalid_argument("An original pin lies on an inactive routing layer: " + net.name);
                }
                auto cells = graph.rangeSearchCells(shape);
                for (int x = cells.lx(); x <= cells.hx(); x++) for (int y = cells.ly(); y <= cells.hy(); y++) {
                    GRPoint point(shape.layerIdx, x, y);
                    int access = 1;
                    if (point.layerIdx >= parameters.min_routing_layer) {
                        unsigned direction = graph.getLayerDirection(point.layerIdx);
                        access = graph.getEdge(point.layerIdx, x, y).capacity >= 1;
                        if (point[direction] > 0) {
                            auto lower = point; lower[direction]--;
                            access += graph.getEdge(lower.layerIdx, lower.x, lower.y).capacity >= 1;
                        }
                    }
                    DBU dist = utils::Dist(cellCenter(graph, point), utils::PointT<DBU>(net.bounds.cx(), net.bounds.cy()));
                    if (access > bestAccess || (access == bestAccess && dist < bestDistance)) {
                        best = point; bestAccess = access; bestDistance = dist;
                    }
                }
            }
            if (best.layerIdx < 0) throw std::runtime_error("No pin access shape for net " + net.name);
            net.access.push_back(best);
        }
        net.id = nets.size(); nets.push_back(std::move(net));
    }
    std::stable_sort(nets.begin(), nets.end(), [](const OriginalNet& a, const OriginalNet& b) { return a.bounds.hp() < b.bounds.hp(); });
    if (!parameters.h3d_netlist_file.empty()) {
        std::ifstream input(parameters.h3d_netlist_file);
        if (!input) throw std::runtime_error("Cannot read cross-die connectivity manifest");
        Rsyn::Json manifest; input >> manifest;
        std::map<std::string, const OriginalNet*> byName;
        for (const auto& net : nets) byName[net.name] = &net;
        std::set<std::string> verified;
        for (const auto& item : manifest.at("nets")) {
            auto name = item.at("original_net_name").get<std::string>();
            if (!verified.insert(name).second || !byName.count(name) || !byName.at(name)->crossesDies()) {
                throw std::invalid_argument("Manifest has a duplicate or missing cross-die net: " + name);
            }
            const auto& net = *byName.at(name);
            using Endpoint = std::tuple<int, std::string, std::string>;
            std::multiset<Endpoint> expected, actual;
            for (int die = 0; die < 2; die++) for (const auto& pin : item.at(die == 0 ? "bottom_pins" : "top_pins")) {
                expected.emplace(die, pin.at("instance").get<std::string>(), pin.at("pin").get<std::string>());
            }
            for (const auto& pin : net.pins) {
                int die = static_cast<int>(pin.die);
                auto names = dies[die].design->getPinNames({pin.instanceIndex, pin.pinIndex});
                actual.emplace(die, pin.pinIndex == -1 ? "PIN" : names.first, pin.pinIndex == -1 ? names.first : names.second);
            }
            if (expected != actual) throw std::invalid_argument("Manifest endpoints differ from loaded DEF endpoints: " + name);
        }
        for (const auto& net : nets) if (net.crossesDies() && !verified.count(net.name)) {
            throw std::invalid_argument("Loaded cross-die net is absent from manifest: " + net.name);
        }
        log() << "Verified exact cross-die manifest endpoints for " << verified.size() << " nets" << std::endl;
    }
    PatternRoute::readFluteLUT();
}

H3DTree H3DDesign::planarTree(const OriginalNet& net) const {
    using Key = std::pair<DBU, DBU>;
    std::map<Key, std::vector<PinReference>> pins;
    for (std::size_t i = 0; i < net.pins.size(); i++) {
        auto p = cellCenter(dies[static_cast<int>(net.pins[i].die)].router->getGridGraph(), net.access[i]);
        pins[{p.x, p.y}].push_back(net.pins[i]);
    }
    H3DTree result;
    std::map<Key, int> indices;
    auto nodeAt = [&](DBU x, DBU y) {
        Key key(x, y);
        auto found = indices.find(key);
        if (found != indices.end()) return found->second;
        int index = result.addNode({x, y}, pins.count(key) ? pins.at(key) : std::vector<PinReference>());
        indices[key] = index;
        return index;
    };
    if (pins.size() == 1) { nodeAt(pins.begin()->first.first, pins.begin()->first.second); return result; }
    std::vector<int> xs, ys;
    for (const auto& entry : pins) {
        if (entry.first.first < std::numeric_limits<int>::min() || entry.first.first > std::numeric_limits<int>::max() ||
            entry.first.second < std::numeric_limits<int>::min() || entry.first.second > std::numeric_limits<int>::max()) {
            throw std::out_of_range("FLUTE input coordinate exceeds its integer range");
        }
        xs.push_back(entry.first.first); ys.push_back(entry.first.second);
    }
    Tree tree = flute(xs.size(), xs.data(), ys.data(), ACCURACY);
    std::unique_ptr<Branch, decltype(&free)> branches(tree.branch, &free);
    std::set<std::pair<int, int>> edges;
    for (int i = 0; i < 2 * tree.deg - 2; i++) nodeAt(tree.branch[i].x, tree.branch[i].y);
    for (int i = 0; i < 2 * tree.deg - 2; i++) {
        auto& a = tree.branch[i]; auto& b = tree.branch[a.n];
        int u = nodeAt(a.x, a.y), v = nodeAt(b.x, b.y);
        if (u != v) edges.insert(std::minmax(u, v));
    }
    for (const auto& edge : edges) result.addEdge(edge.first, edge.second);
    result.rootAt();
    return result;
}

} // namespace h3d
