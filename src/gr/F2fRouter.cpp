#include "F2fRouter.h"
#include "F2fCandidateSearch.h"
#include "F2fPareto.h"
#include "GlobalRouter.h"
#include "MazeRoute.h"
#include "PatternRoute.h"
#include <array>
#include <boost/filesystem.hpp>
#include <chrono>
#include <map>
#include <numeric>

namespace {

using XY = utils::PointT<DBU>;
using Box = utils::BoxT<DBU>;
using Json = Rsyn::Json;
enum Die { Top, Bottom };

class Diagnostics {
public:
    explicit Diagnostics(const Json& config) {
        const auto path = config.value("diagnostics", std::string());
        if (path.empty()) return;
        const auto output = boost::filesystem::absolute(path).lexically_normal();
        for (const auto& other : {config.at("top").at("guide").get<std::string>(),
                                  config.at("bottom").at("guide").get<std::string>(),
                                  config.at("hbt_output").get<std::string>(),
                                  config.at("top").at("def").get<std::string>(),
                                  config.at("bottom").at("def").get<std::string>(),
                                  config.at("top").at("lef").get<std::string>(),
                                  config.at("bottom").at("lef").get<std::string>()}) {
            if (output == boost::filesystem::absolute(other).lexically_normal() ||
                (boost::filesystem::exists(output) && boost::filesystem::exists(other) &&
                 boost::filesystem::equivalent(output, other))) {
                throw std::runtime_error("F2F diagnostics must not overwrite an input or routing output");
            }
        }
        stream.open(path);
        if (!stream) throw std::runtime_error("Cannot open F2F diagnostics: " + path);
    }
    bool enabled() const { return stream.is_open(); }
    void write(const Json& event) {
        stream << event.dump() << '\n';
        if (!stream) throw std::runtime_error("Cannot write F2F diagnostics");
    }
private:
    std::ofstream stream;
};

XY point(const Json& value) {
    if (!value.is_array() || value.size() != 2 ||
        !value.at(0).is_number_integer() || !value.at(1).is_number_integer()) {
        throw std::runtime_error("Expected a pair of integer DBU coordinates");
    }
    return {value.at(0).get<DBU>(), value.at(1).get<DBU>()};
}

struct DieGrid {
    Design design;
    GlobalRouter router;
    XY offset;
    XY sign;
    DBU scale;
    int interfaceLayer;
    std::string guideFile;
    robin_hood::unordered_map<std::string, const Net*> localNets;

    DieGrid(const Parameters& params, const Json& input, DBU packageDbu)
        : design(params, input.at("lef").get<std::string>(), input.at("def").get<std::string>()),
          router(design, params, false),
          offset(point(input.value("offset", Json::array({0, 0})))),
          sign(point(input.value("sign", Json::array({1, 1})))),
          scale(packageDbu / design.getLibDBU()), interfaceLayer(-1),
          guideFile(input.at("guide").get<std::string>()) {
        if (scale < 1 || packageDbu % design.getLibDBU() ||
            std::abs(sign.x) != 1 || std::abs(sign.y) != 1) {
            throw std::runtime_error("Invalid F2F coordinate transform or package DBU");
        }
        const auto layer = input.at("layer").get<std::string>();
        for (int i = params.min_routing_layer; i < design.getNumLayers(); ++i) {
            if (design.getLayer(i).getName() == layer) interfaceLayer = i;
        }
        if (interfaceLayer < 0 || design.getNumLayers() <= params.min_routing_layer + 1) {
            throw std::runtime_error("Invalid F2F interface layer: " + layer);
        }
        for (const auto& net : design.getAllNets()) localNets.emplace(net.getName(), &net);
    }

    GridGraph& graph() { return router.getGridGraph(); }
    const GridGraph& graph() const { return router.getGridGraph(); }

    XY packagePoint(const GRPoint& pin) const {
        const auto box = packageBox(graph().getCellBox(pin));
        return {box.cx(), box.cy()};
    }

    XY localPackageUnits(const XY& position) const {
        return {sign.x * (position.x - offset.x), sign.y * (position.y - offset.y)};
    }

    Box packageBox(const Box& box) const {
        Box result;
        for (int axis = 0; axis < 2; ++axis) {
            result[axis].Update(offset[axis] + sign[axis] * scale * box[axis].low);
            result[axis].Update(offset[axis] + sign[axis] * scale * box[axis].high);
        }
        return result;
    }

    GRPoint landing(const XY& position) const {
        const auto local = localPackageUnits(position);
        int cells[2];
        for (int axis = 0; axis < 2; ++axis) {
            const auto& lines = design.getGridlines()[axis];
            auto it = std::upper_bound(lines.begin(), lines.end(), local[axis],
                [this](DBU value, DBU line) { return value < line * scale; });
            cells[axis] = std::max(0, std::min<int>(it - lines.begin() - 1, lines.size() - 2));
        }
        return {interfaceLayer, cells[0], cells[1]};
    }
};

struct HbtEdge {
    XY position;
    std::array<GRPoint, 2> ends;
    int owner = -1;
    bool fixed = false;
};

struct RoutedNet {
    std::array<vector<GRNet>, 2> components;
    vector<int> sites;
    CostT cost = std::numeric_limits<CostT>::infinity();
    double wireOverflow = 0, resourceOverflow = 0;
};

struct Job {
    std::array<std::unique_ptr<GRNet>, 2> sides;
    RoutedNet route;
    int fixedSite = -1;
    Box searchRegion;
    std::vector<F2fParetoRecord> paretoArchive;

    bool crosses() const { return sides[Top] && sides[Bottom]; }
};

struct CandidatePolicy {
    bool budgetDp, beam, compareOld, uniqueSteps;
    int maxNodes, beamWidth, maxSites;

    explicit CandidatePolicy(const Json& config) {
        const auto mode = config.value("candidate_mode", std::string("improved"));
        if (mode != "legacy" && mode != "improved") throw std::runtime_error("Invalid candidate_mode");
        const bool improved = mode == "improved";
        const auto opts = config.value("candidate_search", Json::object());
        budgetDp = improved && opts.value("budget_dp", true);
        beam = improved && opts.value("position_beam", true);
        compareOld = improved && opts.value("compare_old", true);
        uniqueSteps = improved && opts.value("deduplicate_steps", true);

        // max_hbt is intentionally no longer used. The DP derives its HBT
        // count range from the projected tree's possible crossing events.
        maxNodes = opts.value("max_projection_nodes", 160);
        beamWidth = opts.value("beam_width", 3);
        maxSites = opts.value("max_sites", 32);
        if (maxNodes < 2 || maxNodes > 512 ||
            beamWidth < 1 || beamWidth > 16 || maxSites < 2 || maxSites > 128) {
            throw std::runtime_error("Invalid F2F candidate search bounds");
        }
    }
};

class F2fGraph {
public:
    std::array<DieGrid, 2> dies;
    vector<HbtEdge> sites;
    CostT hbtCost;
    DBU packageDbu;
    RTree siteIndex;
    Diagnostics diagnostics;
    CandidatePolicy policy;

    F2fGraph(const Parameters& params, const Json& config)
        : dies{{DieGrid(params, config.at("top"), config.at("package_dbu").get<DBU>()),
                DieGrid(params, config.at("bottom"), config.at("package_dbu").get<DBU>())}},
          hbtCost(config.at("hbt").value("cost", 4.0)),
          packageDbu(config.at("package_dbu").get<DBU>()), diagnostics(config), policy(config) {
        const auto& hbt = config.at("hbt");
        const XY origin = point(hbt.at("origin"));
        const XY pitch = point(hbt.at("pitch"));
        const DBU spacing = hbt.value("min_spacing", std::min(pitch.x, pitch.y));
        if (spacing <= 0 || pitch.x < spacing || pitch.y < spacing ||
            !std::isfinite(hbtCost) || hbtCost < 0) {
            throw std::runtime_error("Invalid HBT pitch, spacing or cost");
        }
        const Box overlap = dies[Top].packageBox(dies[Top].design.getDieRegion())
            .IntersectWith(dies[Bottom].packageBox(dies[Bottom].design.getDieRegion()));
        if (!overlap.IsValid()) throw std::runtime_error("No overlap between F2F dies");
        vector<Box> keepouts;
        for (const auto& entry : hbt.value("keepouts", Json::array())) {
            const XY low = point(entry.at(0)), high = point(entry.at(1));
            if (low.x > high.x || low.y > high.y) throw std::runtime_error("Invalid HBT keepout");
            keepouts.emplace_back(low.x, low.y, high.x, high.y);
        }
        auto first = [&](int axis) {
            const DBU remainder = (origin[axis] - overlap[axis].low) % pitch[axis];
            return overlap[axis].low + (remainder < 0 ? remainder + pitch[axis] : remainder);
        };
        for (DBU x = first(0); x <= overlap.x.high; x += pitch.x) {
            for (DBU y = first(1); y <= overlap.y.high; y += pitch.y) {
                const Box location(x, y, x, y);
                if (std::any_of(keepouts.begin(), keepouts.end(),
                    [&](const Box& box) { return box.HasIntersectWith(location); })) continue;
                int id = sites.size();
                sites.push_back({{x, y}, {{dies[Top].landing({x, y}), dies[Bottom].landing({x, y})}}});
                const boostPoint position(x, y);
                siteIndex.insert({boostBox(position, position), id});
            }
        }
    }

    int reserveFixed(const XY& position, int job) {
        vector<RTree::value_type> matches;
        const boostPoint p(position.x, position.y);
        siteIndex.query(bgi::intersects(boostBox(p, p)), std::back_inserter(matches));
        if (matches.size() != 1 || sites[matches.front().second].owner >= 0) {
            throw std::runtime_error("Fixed HBT is illegal or already reserved");
        }
        const int id = matches.front().second;
        sites[id].owner = job;
        sites[id].fixed = true;
        return id;
    }

    vector<int> candidates(const Box& region, int id, std::size_t count) const {
        vector<int> result;
        auto available = [&](const RTree::value_type& entry) {
            return sites[entry.second].owner < 0 || sites[entry.second].owner == id;
        };
        const boostBox query(boostPoint(region.x.low, region.y.low), boostPoint(region.x.high, region.y.high));
        auto add = [&](const auto& predicate, std::size_t limit) {
            for (auto it = siteIndex.qbegin(predicate && bgi::satisfies(available));
                 it != siteIndex.qend() && limit > 0; ++it) {
                if (std::find(result.begin(), result.end(), it->second) == result.end()) {
                    result.push_back(it->second);
                    --limit;
                }
            }
        };
        add(bgi::intersects(query), count / 2);
        const std::array<DBU, 3> xs{{region.x.low, region.cx(), region.x.high}};
        const std::array<DBU, 3> ys{{region.y.low, region.cy(), region.y.high}};
        for (DBU x : xs) for (DBU y : ys) {
            add(bgi::nearest(boostPoint(x, y), count), std::max<std::size_t>(1, count / 9));
        }
        auto lowerBound = [&](int site) {
            const auto& p = sites[site].position;
            return std::max({DBU(0), region.x.low - p.x, p.x - region.x.high}) +
                   std::max({DBU(0), region.y.low - p.y, p.y - region.y.high});
        };
        std::sort(result.begin(), result.end(), [&](int a, int b) {
            return std::make_pair(lowerBound(a), a) < std::make_pair(lowerBound(b), b);
        });
        if (result.size() > count) result.resize(count);
        return result;
    }

    void apply(int id, const RoutedNet& route, bool reverse) {
        for (int index : route.sites) {
            const auto& site = sites.at(index);
            if ((reverse && site.owner != id) || (!reverse && site.owner >= 0 && site.owner != id)) {
                throw std::runtime_error("HBT ownership conflict");
            }
        }
        for (int die = Top; die <= Bottom; ++die) {
            dies[die].graph().commitForest(route.components[die], reverse, policy.uniqueSteps);
        }
        for (int index : route.sites) {
            if (!sites[index].fixed) sites[index].owner = reverse ? -1 : id;
        }
    }

};

const Net& localNet(const DieGrid& die, const std::string& name) {
    const auto found = die.localNets.find(name);
    if (found == die.localNets.end()) throw std::runtime_error("Unknown local net: " + name);
    return *found->second;
}

std::unique_ptr<GRNet> makeNet(DieGrid& die, const Net& original, const std::string& cut) {
    Net net(original.getIndex(), original.getName());
    bool removed = false;
    for (const auto& pin : original.getAllPinRefs()) {
        const auto& instance = die.design.getInstance(pin.instanceIndex);
        if (!cut.empty() && !instance.isCell() && instance.getName() == cut) {
            const auto& names = die.design.getPortNetNames();
            const auto entry = names.find(cut);
            if (entry == names.end() || entry->second != original.getName()) {
                throw std::runtime_error("PINS/NETS disagree for cut port: " + cut);
            }
            removed = true;
        } else {
            net.addPinRef(pin.instanceIndex, pin.pinIndex);
        }
    }
    if (!cut.empty() && !removed) throw std::runtime_error("Missing cut port " + cut + " on " + original.getName());
    auto result = std::make_unique<GRNet>(net, die.design, die.graph());
    for (const auto& points : result->getPinAccessPoints()) {
        if (points.empty()) throw std::runtime_error("Pin has no access point on net: " + net.getName());
    }
    return result;
}

Box medianRegion(Job& job, F2fGraph& graph) {
    std::array<vector<DBU>, 2> coords;
    for (int d = Top; d <= Bottom; ++d) {
        auto& die = graph.dies[d];
        for (const auto& points : job.sides[d]->getPinAccessPoints()) {
            const auto box = die.packageBox(die.graph().getCellBox(points.front()));
            coords[0].push_back(box.cx());
            coords[1].push_back(box.cy());
        }
    }
    Box region;
    for (int axis = 0; axis < 2; ++axis) {
        auto& values = coords[axis];
        if (values.empty()) throw std::runtime_error("Cross-die net has no real terminals");
        std::sort(values.begin(), values.end());
        region[axis].Set(values[(values.size() - 1) / 2], values[values.size() / 2]);
    }
    return region;
}

vector<Job> buildJobs(F2fGraph& graph, const Json& config) {
    Json connections = config.value("connections", Json::array());
    if (config.value("paired_ports", false)) {
        if (!connections.empty()) throw std::runtime_error("Choose paired_ports or explicit connections");
        const auto& topPorts = graph.dies[Top].design.getPortNetNames();
        const auto& bottomPorts = graph.dies[Bottom].design.getPortNetNames();
        std::vector<std::string> portNames;
        for (const auto& port : topPorts) {
            auto peer = bottomPorts.find(port.first);
            if (peer != bottomPorts.end() && peer->second == port.second) portNames.push_back(port.first);
        }
        std::sort(portNames.begin(), portNames.end());
        for (const auto& port : portNames) {
            const auto& name = topPorts.at(port);
            connections.push_back({{"top_net", name}, {"bottom_net", name},
                                   {"top_cut_port", port}, {"bottom_cut_port", port}});
        }
    }

    vector<Job> jobs;
    std::array<std::set<int>, 2> mapped;
    for (const auto& entry : connections) {
        Job job;
        for (int d = Top; d <= Bottom; ++d) {
            auto& die = graph.dies[d];
            const std::string prefix = d == Top ? "top" : "bottom";
            const auto& net = localNet(die, entry.at(prefix + "_net").get<std::string>());
            if (!mapped[d].insert(net.getIndex()).second) {
                throw std::runtime_error("Local net appears in multiple F2F mappings: " + net.getName());
            }
            job.sides[d] = makeNet(die, net, entry.value(prefix + "_cut_port", std::string()));
            if (job.sides[d]->getNumPins() == 0) {
                throw std::runtime_error("No real terminals remain on mapped net: " + net.getName());
            }
        }
        if (entry.count("fixed_site")) job.fixedSite = graph.reserveFixed(point(entry.at("fixed_site")), jobs.size());
        job.searchRegion = medianRegion(job, graph);
        jobs.push_back(std::move(job));
    }
    for (int d = Top; d <= Bottom; ++d) {
        auto& die = graph.dies[d];
        for (const auto& net : die.design.getAllNets()) {
            if (mapped[d].count(net.getIndex()) || net.getAllPinRefs().empty()) continue;
            Job job;
            job.sides[d] = makeNet(die, net, "");
            jobs.push_back(std::move(job));
        }
    }
    return jobs;
}

enum class Stage { Pattern, Detour, Maze };

int phase(Stage stage) { return 1 + static_cast<int>(stage); }

Json routeDiagnostics(const RoutedNet& route, F2fGraph& graph, bool includeOverflow = true) {
    Json sides = Json::array();
    double total = graph.hbtCost * route.sites.size();
    for (int d = Top; d <= Bottom; ++d) {
        const auto stat = graph.dies[d].graph().measureForest(
            route.components[d], includeOverflow, graph.policy.uniqueSteps);
        total += stat.wireCost + stat.viaCost + stat.congestionCost;
        sides.push_back({{"wire_um", stat.wirelengthUm}, {"vias", stat.vias},
                         {"wire_cost", stat.wireCost}, {"via_cost", stat.viaCost},
                         {"congestion_cost", stat.congestionCost},
                         {"wire_overflow_delta", stat.wireOverflowDelta},
                         {"resource_overflow_delta", stat.resourceOverflowDelta},
                         {"duplicate_wires", stat.duplicateWires},
                         {"duplicate_wire_um", stat.duplicateWirelengthUm},
                         {"duplicate_vias", stat.duplicateVias}});
    }
    return {{"sites", route.sites}, {"dies", sides}, {"cost", total},
            {"hbt_cost", graph.hbtCost * route.sites.size()}};
}

struct Terminal {
    int die;
    int pin;
};

struct Component {
    int die;
    vector<int> pins;
};

struct Crossing {
    std::array<int, 2> components;
    Box region;
};

struct Topology {
    vector<Component> components;
    vector<Crossing> crossings;
};

Topology oneCrossing(const Job& job) {
    Topology topology;
    for (int d = Top; d <= Bottom; ++d) {
        if (!job.sides[d]) continue;
        topology.components.push_back({d, {}});
        for (int p = 0; p < job.sides[d]->getNumPins(); ++p) {
            topology.components.back().pins.push_back(p);
        }
    }
    if (job.crosses()) topology.crossings.push_back({{{0, 1}}, job.searchRegion});
    return topology;
}

// The FLUTE tree is a planar projection. Pins at identical package coordinates
// retain all of their original die and pin identities.
struct ProjectedNode {
    XY point;
    vector<Terminal> terminals;
    vector<int> children;
};

vector<ProjectedNode> projectSteiner(const Job& job, F2fGraph& graph) {
    std::map<std::pair<DBU, DBU>, vector<Terminal>> pins;
    for (int d = Top; d <= Bottom; ++d) {
        if (!job.sides[d]) continue;
        auto& die = graph.dies[d];
        const auto& accesses = job.sides[d]->getPinAccessPoints();
        for (int p = 0; p < accesses.size(); ++p) {
            const auto box = die.packageBox(die.graph().getCellBox(accesses[p].front()));
            pins[{box.cx(), box.cy()}].push_back({d, p});
        }
    }
    if (pins.size() == 1) {
        auto& pin = *pins.begin();
        return {{{pin.first.first, pin.first.second}, std::move(pin.second), {}}};
    }

    const auto xmin = pins.begin()->first.first;
    const auto xmax = pins.rbegin()->first.first;
    auto ymin = pins.begin()->first.second;
    auto ymax = ymin;
    for (const auto& pin : pins) {
        ymin = std::min(ymin, pin.first.second);
        ymax = std::max(ymax, pin.first.second);
    }
    // FLUTE uses signed int for both coordinates and accumulated wirelength.
    const DBU bound = std::numeric_limits<int>::max() / (4 * static_cast<DBU>(pins.size()));
    if (xmax - xmin > bound || ymax - ymin > bound) return {};

    vector<int> xs, ys;
    for (const auto& pin : pins) {
        xs.push_back(pin.first.first - xmin);
        ys.push_back(pin.first.second - ymin);
    }
    Tree tree = flute(xs.size(), xs.data(), ys.data(), ACCURACY);
    std::unique_ptr<Branch, decltype(&std::free)> branches(tree.branch, &std::free);
    vector<vector<int>> adjacent(2 * tree.deg - 2);
    for (int i = 0; i < adjacent.size(); ++i) {
        const int n = tree.branch[i].n;
        if (i == n) continue;
        adjacent[i].push_back(n);
        adjacent[n].push_back(i);
    }
    vector<ProjectedNode> nodes;
    auto visit = [&](auto&& self, int branch, int parentBranch, int parent) -> void {
        const auto& b = tree.branch[branch];
        const XY xy(b.x + xmin, b.y + ymin);
        int node = parent;
        if (parent < 0 || nodes[parent].point != xy) {
            node = nodes.size();
            nodes.push_back({xy, {}, {}});
            if (parent >= 0) nodes[parent].children.push_back(node);
        }
        auto found = pins.find({xy.x, xy.y});
        if (found != pins.end()) {
            nodes[node].terminals = std::move(found->second);
            pins.erase(found);
        }
        for (int child : adjacent[branch]) {
            if (child != parentBranch) self(self, child, branch, node);
        }
    };
    visit(visit, 0, -1, -1);
    if (!pins.empty()) throw std::runtime_error("Projected FLUTE tree lost a terminal");
    return nodes;
}

Topology labeledTopology(const vector<ProjectedNode>& nodes, const vector<int>& labels) {
    Topology topology;
    auto component = [&](int d) {
        int id = topology.components.size();
        topology.components.push_back({d, {}});
        return id;
    };
    auto cross = [&](int current, int other, const Box& region) {
        if (topology.components[current].die == Bottom) std::swap(current, other);
        topology.crossings.push_back({{{current, other}}, region});
    };
    auto visit = [&](auto&& self, int i, int d, int current) -> void {
        const auto& n = nodes[i];
        int opposite = -1;
        if (std::any_of(n.terminals.begin(), n.terminals.end(),
                        [&](const Terminal& terminal) { return terminal.die != d; })) {
            opposite = component(1 - d);
            cross(current, opposite, Box(n.point.x, n.point.y, n.point.x, n.point.y));
        }
        for (const auto& terminal : n.terminals) {
            topology.components[terminal.die == d ? current : opposite].pins.push_back(terminal.pin);
        }
        for (int child : n.children) {
            if (labels[child] == d) {
                self(self, child, d, current);
            } else {
                const auto p = nodes[child].point;
                const int next = component(1 - d);
                cross(current, next, Box(std::min(p.x, n.point.x), std::min(p.y, n.point.y),
                                         std::max(p.x, n.point.x), std::max(p.y, n.point.y)));
                self(self, child, 1 - d, next);
            }
        }
    };
    visit(visit, 0, labels[0], component(labels[0]));
    return topology;
}

// Label Steiner vertices by die, then cut at die changes. The local mismatch
// term allows one HBT to serve all opposite-side pins at the same vertex.
Topology labelTopology(const vector<ProjectedNode>& nodes, bool pinAffinity) {
    vector<std::array<int, 2>> costs(nodes.size());
    vector<std::array<bool, 2>> hasPins(nodes.size(), {{false, false}});
    for (int i = static_cast<int>(nodes.size()) - 1; i >= 0; --i) {
        for (const auto& pin : nodes[i].terminals) hasPins[i][pin.die] = true;
        for (int d = Top; d <= Bottom; ++d) {
            costs[i][d] = (pinAffinity ? 3 : 1) * hasPins[i][1 - d];
            for (int child : nodes[i].children) {
                costs[i][d] += std::min(costs[child][d], 1 + costs[child][1 - d]);
            }
        }
    }
    vector<int> labels(nodes.size());
    labels[0] = costs[0][Top] <= costs[0][Bottom] ? Top : Bottom;
    for (int v = 0; v < nodes.size(); ++v) {
        const int d = labels[v];
        for (int child : nodes[v].children) {
            labels[child] = costs[child][d] <= 1 + costs[child][1 - d] ? d : 1 - d;
        }
    }
    return labeledTopology(nodes, labels);
}

DBU distanceToRegion(const XY& p, const Box& b) {
    return std::max({DBU(0), b.x.low - p.x, p.x - b.x.high}) +
           std::max({DBU(0), b.y.low - p.y, p.y - b.y.high});
}

double estimatedWire(const DieGrid& die, const XY& a, const XY& b, const Parameters& params) {
    const auto& grid = die.graph();
    const auto midpoint = die.landing({a.x + (b.x - a.x) / 2, a.y + (b.y - a.y) / 2});
    double estimate = 0;
    for (int axis = 0; axis < 2; ++axis) {
        const double span = std::abs(a[axis] - b[axis]) / double(die.scale);
        if (span == 0) continue;
        double perDbu = std::numeric_limits<double>::infinity();
        if (grid.getSize(axis) > 1) {
            auto lower = midpoint;
            lower[axis] = std::min(lower[axis], static_cast<int>(grid.getSize(axis)) - 2);
            auto upper = lower;
            ++upper[axis];
            for (int layer = params.min_routing_layer; layer < grid.getNumLayers(); ++layer) {
                if (grid.getLayerDirection(layer) != axis) continue;
                perDbu = std::min(perDbu, grid.getWireCost(layer, lower, upper) /
                                            grid.getEdgeLength(axis, lower[axis]));
            }
        }
        // 单点采样只用于排序，完整方案仍走实际 DAG/Maze。
        if (!std::isfinite(perDbu)) perDbu = params.weight_wire_length / grid.getM2Pitch();
        estimate += perDbu * span;
    }
    return estimate;
}

double estimatedAccess(const DieGrid& die, const GRPoint& landing, int target) {
    const auto& grid = die.graph();
    double cost = 0;
    for (int l = std::min(landing.layerIdx, target); l < std::max(landing.layerIdx, target); ++l) {
        if (!grid.canUseVia(l)) return std::numeric_limits<double>::infinity();
        cost += grid.getViaCost(l, landing);
    }
    return cost;
}

double approach(const DieGrid& die, const GRPoint& landing, int axis, const Parameters& params) {
    if (axis < 0) return 0;
    const auto& grid = die.graph();
    double best = std::numeric_limits<double>::infinity();
    for (int l = params.min_routing_layer; l < grid.getNumLayers(); ++l) {
        if (grid.getLayerDirection(l) == static_cast<unsigned>(axis)) {
            best = std::min(best, estimatedAccess(die, landing, l));
        }
    }
    return best;
}

vector<Topology> budgetTopologies(const vector<ProjectedNode>& tree, const Job& job,
                                   const F2fGraph& graph, const Parameters& params) {
    vector<F2fLabelNode> input(tree.size());
    for (int i = 0; i < tree.size(); ++i) {
        const auto& node = tree[i];
        for (int d = Top; d <= Bottom; ++d) {
            auto& label = input[i];
            for (const auto& pin : node.terminals) {
                if (pin.die == d) continue;
                label.localHbt[d] = 1;
                auto& otherDie = graph.dies[pin.die];
                double access = std::numeric_limits<double>::infinity();
                for (const auto& p : job.sides[pin.die]->getPinAccessPoints()[pin.pin]) {
                    access = std::min(access, estimatedAccess(otherDie, otherDie.landing(node.point), p.layerIdx));
                }
                label.localCost[d] = std::max(label.localCost[d], graph.hbtCost + access);
            }
        }
        for (int child : node.children) {
            const auto& next = tree[child].point;
            const XY midpoint(node.point.x + (next.x - node.point.x) / 2,
                              node.point.y + (next.y - node.point.y) / 2);
            const auto deltaX = std::abs(next.x - node.point.x), deltaY = std::abs(next.y - node.point.y);
            const int axis = deltaX + deltaY == 0 ? -1 : deltaX >= deltaY ? 0 : 1;
            F2fLabelNode::Child edge{child, {}};
            for (int d = Top; d <= Bottom; ++d) {
                const auto& a = graph.dies[d];
                const auto& b = graph.dies[1 - d];
                edge.cost[d][d] = estimatedWire(a, node.point, next, params);
                edge.cost[d][1 - d] = graph.hbtCost + estimatedWire(a, node.point, midpoint, params) +
                    estimatedWire(b, midpoint, next, params) + approach(a, a.landing(midpoint), axis, params) +
                    approach(b, b.landing(midpoint), axis, params);
            }
            input[i].children.push_back(edge);
        }
    }
    vector<Topology> topologies;
    for (auto& state : searchF2fDieAssignments(input)) {
        auto topology = labeledTopology(tree, state.labels);
        if (topology.crossings.size() != state.hbtCount) {
            throw std::logic_error("F2F budget DP traceback disagrees with HBT count");
        }
        topologies.push_back(std::move(topology));
    }
    return topologies;
}

bool validTopology(const Topology& topology, const Job& job, const vector<int>& sites,
                   const F2fGraph& graph, int id) {
    if (sites.size() != topology.crossings.size() || sites.size() + 1 != topology.components.size() ||
        (job.fixedSite >= 0 && std::find(sites.begin(), sites.end(), job.fixedSite) == sites.end())) return false;
    vector<int> roots(topology.components.size());
    std::iota(roots.begin(), roots.end(), 0);
    auto root = [&](int x) {
        while (roots[x] != x) x = roots[x];
        return x;
    };
    std::set<int> used;
    for (int i = 0; i < sites.size(); ++i) {
        const int site = sites[i];
        if (site < 0 || site >= graph.sites.size() || !used.insert(site).second) return false;
        const auto& hbt = graph.sites[site];
        if (hbt.owner >= 0 && hbt.owner != id) return false;
        const auto& components = topology.crossings[i].components;
        for (int d = Top; d <= Bottom; ++d) {
            if (components[d] < 0 || components[d] >= roots.size() ||
                topology.components[components[d]].die != d) return false;
        }
        const auto a = root(components[0]), b = root(components[1]);
        if (a == b) return false;
        roots[a] = b;
    }
    for (int d = Top; d <= Bottom; ++d) {
        vector<bool> pins(job.sides[d] ? job.sides[d]->getNumPins() : 0, false);
        for (const auto& component : topology.components) {
            if (component.die != d) continue;
            for (int pin : component.pins) {
                if (pin < 0 || pin >= pins.size() || pins[pin]) return false;
                pins[pin] = true;
            }
        }
        if (std::find(pins.begin(), pins.end(), false) != pins.end()) return false;
    }
    return true;
}

RoutedNet evaluateTopology(const Topology& topology, const vector<int>& sites, const Job& job,
                           F2fGraph& graph, const Parameters& params, Stage stage,
                           const std::array<GridGraphView<bool>, 2>& congestion) {
    RoutedNet result;
    result.sites = sites;
    result.cost = graph.hbtCost * sites.size();
    vector<vector<GRPoint>> landings(topology.components.size());
    for (int i = 0; i < sites.size(); ++i) {
        for (int d = Top; d <= Bottom; ++d) {
            landings[topology.crossings[i].components[d]].push_back(graph.sites[sites[i]].ends[d]);
        }
    }

    for (int i = 0; i < topology.components.size(); ++i) {
        const auto& component = topology.components[i];
        const int d = component.die;
        GRNet net(job.sides[d]->getName());
        const auto& pins = job.sides[d]->getPinAccessPoints();
        for (int pin : component.pins) net.addPinAccessPoints(pins[pin]);
        for (const auto& landing : landings[i]) net.addAccessPoint(landing);

        auto& grid = graph.dies[d].graph();
        if (stage == Stage::Maze) {
            const GRPoint* root = landings[i].empty() ? nullptr : &landings[i].front();
            net.setRoutingTree(routeLayeredMaze(net, grid, params.min_routing_layer, root));
        } else {
            PatternRoute pattern(net, grid, params);
            pattern.constructSteinerTree();
            pattern.constructRoutingDAG();
            if (stage == Stage::Detour) pattern.constructDetours(congestion[d]);
            pattern.run();
        }
        if (!net.getRoutingTree()) return {};
        result.components[d].push_back(std::move(net));
    }
    for (int d = Top; d <= Bottom; ++d) {
        result.cost += graph.dies[d].graph().getForestCost(result.components[d], graph.policy.uniqueSteps);
    }
    return result;
}

void measureReroute(RoutedNet& route, const F2fGraph& graph) {
    route.wireOverflow = 0;
    route.resourceOverflow = 0;
    for (int d = Top; d <= Bottom; ++d) {
        const auto metrics = graph.dies[d].graph().measureForest(route.components[d], true, graph.policy.uniqueSteps);
        route.wireOverflow += metrics.wireOverflowDelta;
        route.resourceOverflow += metrics.resourceOverflowDelta;
    }
}

F2fParetoMetrics measureF2fParetoMetrics(const RoutedNet& route, const F2fGraph& graph) {
    F2fParetoMetrics result;
    result.hbtCount = static_cast<int>(route.sites.size());
    for (int d = Top; d <= Bottom; ++d) {
        const auto metrics = graph.dies[d].graph().measureForest(
            route.components[d], true, graph.policy.uniqueSteps);
        result.wireLengthUm += metrics.wirelengthUm;
        result.viaCount += metrics.vias;
        result.wireOverflowDelta += metrics.wireOverflowDelta;
        result.resourceOverflowDelta += metrics.resourceOverflowDelta;
        result.congestionCost += metrics.congestionCost;
    }
    return result;
}

bool improves(const RoutedNet& candidate, const RoutedNet& previous, double eps) {
    if (!std::isfinite(candidate.cost)) return false;
    if (!std::isfinite(previous.cost)) return true;
    if (std::abs(candidate.wireOverflow - previous.wireOverflow) > eps) {
        return candidate.wireOverflow < previous.wireOverflow;
    }
    if (std::abs(candidate.resourceOverflow - previous.resourceOverflow) > eps) {
        return candidate.resourceOverflow < previous.resourceOverflow;
    }
    if (std::abs(candidate.cost - previous.cost) > eps) return candidate.cost < previous.cost;
    return candidate.sites.size() < previous.sites.size();
}

struct SiteCombination {
    vector<int> sites;
    vector<Box> bounds;
    double estimate = 0;
    bool boundary = false;
};

vector<SiteCombination> beamSites(F2fGraph& graph, const Topology& topology, const Job& job,
                                  const vector<vector<int>>& options, const Parameters& params,
                                  int& collisions) {
    const int count = topology.crossings.size();
    vector<int> order(count);
    std::iota(order.begin(), order.end(), 0);
    std::stable_sort(order.begin(), order.end(), [&](int a, int b) {
        return options[a].size() < options[b].size();
    });
    vector<SiteCombination> beam(1), next;
    beam.front().sites.assign(count, -1);
    for (const auto& component : topology.components) {
        const auto& die = graph.dies[component.die];
        Box bounds;
        for (int pin : component.pins) {
            bounds.Update(die.packagePoint(job.sides[component.die]->getPinAccessPoints()[pin].front()));
        }
        beam.front().bounds.push_back(bounds);
    }
    for (int anchor : order) {
        const auto& crossing = topology.crossings[anchor];
        for (const auto& state : beam) {
            for (int rank = 0; rank < options[anchor].size(); ++rank) {
                const int id = options[anchor][rank];
                if (std::find(state.sites.begin(), state.sites.end(), id) != state.sites.end()) {
                    ++collisions;
                    continue;
                }
                SiteCombination candidate = state;
                candidate.sites[anchor] = id;
                candidate.boundary |= rank >= (options[anchor].size() + 1) / 2;
                const auto& site = graph.sites[id];
                for (int d = Top; d <= Bottom; ++d) {
                    const auto& die = graph.dies[d];
                    Box& bounds = candidate.bounds[crossing.components[d]];
                    const DBU oldLength = bounds.IsValid() ? bounds.hp() : 0;
                    const XY oldCenter = bounds.IsValid() ? XY(bounds.cx(), bounds.cy()) : site.position;
                    bounds.Update(site.position);
                    // 分量包围盒的增量近似多落点共享路径；这里只排序，不当作真实树长。
                    candidate.estimate += params.weight_wire_length * (bounds.hp() - oldLength) /
                        (double(die.scale) * die.graph().getM2Pitch());
                    const auto dx = std::abs(oldCenter.x - site.position.x);
                    const auto dy = std::abs(oldCenter.y - site.position.y);
                    const int axis = dx + dy == 0 ? -1 : dx >= dy ? 0 : 1;
                    candidate.estimate += approach(die, site.ends[d], axis, params);
                    // 采样落点接入方向的局部拥塞，并保留 L1 区域的相对偏移。
                    candidate.estimate += axis < 0 ? 0 :
                        0.1 * estimatedWire(die, oldCenter, site.position, params);
                    candidate.estimate += 0.25 * params.weight_wire_length * distanceToRegion(
                        site.position, crossing.region) / (double(die.scale) * die.graph().getM2Pitch());
                }
                if (std::isfinite(candidate.estimate)) next.push_back(std::move(candidate));
            }
        }
        std::stable_sort(next.begin(), next.end(), [](const auto& a, const auto& b) {
            return a.estimate < b.estimate;
        });
        if (next.size() > graph.policy.beamWidth) next.resize(graph.policy.beamWidth);
        beam.swap(next);
        next.clear();
        if (beam.empty()) break;
    }
    return beam;
}

RoutedNet planJob(F2fGraph& graph, Job& job, int id, const Parameters& params, Stage stage,
                  const std::array<GridGraphView<bool>, 2>& congestion) {
    RoutedNet best;
    job.paretoArchive.clear();

    // Preserve the incumbent when a later stage reroutes an existing net.
    if (stage != Stage::Pattern && std::isfinite(job.route.cost)) {
        bool connected = true;
        for (int d = Top; d <= Bottom; ++d) {
            for (const auto& component : job.route.components[d]) {
                connected = connected && graph.dies[d].graph().connectsPins(component);
            }
        }
        if (connected) {
            F2fParetoRecord incumbent;
            incumbent.metrics = measureF2fParetoMetrics(job.route, graph);
            incumbent.sites = job.route.sites;
            incumbent.stage = phase(stage);
            insertF2fParetoCandidate(job.paretoArchive, std::move(incumbent));
        }
    }

    int single = 0, multi = 0, unavailable = 0, incomplete = 0, expansions = 0, collisions = 0, retained = 0;
    const bool compareOverflow = graph.policy.compareOld && stage != Stage::Pattern;
    const bool check = graph.policy.budgetDp || graph.policy.beam || graph.policy.compareOld || graph.policy.uniqueSteps;
    auto evaluate = [&](const Topology& topology, const vector<int>& sites) {
        RoutedNet plan;
        if (!check || validTopology(topology, job, sites, graph, id)) {
            plan = evaluateTopology(topology, sites, job, graph, params, stage, congestion);
        }
        if (compareOverflow && std::isfinite(plan.cost)) measureReroute(plan, graph);

        // Every candidate entering the Pareto archive must be connected.
        if (std::isfinite(plan.cost) && check) {
            for (int d = Top; d <= Bottom; ++d) {
                for (const auto& component : plan.components[d]) {
                    if (!graph.dies[d].graph().connectsPins(component)) {
                        plan.cost = std::numeric_limits<CostT>::infinity();
                        break;
                    }
                }
                if (!std::isfinite(plan.cost)) break;
            }
        }

        if (std::isfinite(plan.cost) && check) {
            F2fParetoRecord candidate;
            candidate.metrics = measureF2fParetoMetrics(plan, graph);
            candidate.sites = sites;
            candidate.stage = phase(stage);
            insertF2fParetoCandidate(job.paretoArchive, std::move(candidate));
        }

        single += sites.size() == 1;
        multi += sites.size() > 1;
        if (graph.diagnostics.enabled()) {
            Json event = {{"type", "candidate"}, {"net", id}, {"stage", phase(stage)}, {"sites", sites},
                          {"feasible", std::isfinite(plan.cost)}, {"score", plan.cost}};
            if (std::isfinite(plan.cost)) event["metrics"] = routeDiagnostics(plan, graph);
            graph.diagnostics.write(event);
        }
        const double score = plan.cost;
        if (improves(plan, best, 0.0)) {
            best = std::move(plan);
        }
        return score;
    };
    auto finish = [&] {
        if (graph.diagnostics.enabled()) {
            graph.diagnostics.write({{"type", "search"}, {"net", id}, {"stage", phase(stage)},
                {"single_evaluated", single}, {"multi_evaluated", multi}, {"empty_site_lists", unavailable},
                {"incomplete_combinations", incomplete}, {"expansions", expansions},
                {"retained_dp_topologies", retained}, {"beam_collision_rejections", collisions},
                {"feasible", std::isfinite(best.cost)},
                {"pareto_candidates", job.paretoArchive.size()}});
            for (std::size_t rank = 0; rank < job.paretoArchive.size(); ++rank) {
                const auto& candidate = job.paretoArchive[rank];
                const auto& m = candidate.metrics;
                graph.diagnostics.write({{"type", "pareto_candidate"}, {"net", id},
                    {"stage", candidate.stage}, {"rank", rank}, {"sites", candidate.sites},
                    {"metrics", {{"wirelength_um", m.wireLengthUm}, {"hbt_count", m.hbtCount},
                        {"via_count", m.viaCount}, {"wire_overflow_delta", m.wireOverflowDelta},
                        {"resource_overflow_delta", m.resourceOverflowDelta},
                        {"congestion_cost", m.congestionCost}}}});
            }
        }
        return std::move(best);
    };
    const auto base = oneCrossing(job);
    if (!job.crosses()) {
        evaluate(base, {});
        return finish();
    }
    auto fixedAnchor = [&](const Topology& topology) {
        if (job.fixedSite < 0) return -1;
        const auto& p = graph.sites[job.fixedSite].position;
        return static_cast<int>(std::min_element(topology.crossings.begin(), topology.crossings.end(),
            [&](const Crossing& a, const Crossing& b) {
                return distanceToRegion(p, a.region) < distanceToRegion(p, b.region);
            }) - topology.crossings.begin());
    };
    auto searchPositions = [&](const Topology& topology) {
        const int fixed = fixedAnchor(topology);
        int width = std::min(graph.policy.maxSites, stage == Stage::Pattern ? 6 : 10);
        std::set<vector<int>> evaluated;
        double bestScore = std::numeric_limits<double>::infinity();
        bool boundary = false;
        while (true) {
            vector<vector<int>> options;
            for (int i = 0; i < topology.crossings.size(); ++i) {
                if (i == fixed) options.push_back({job.fixedSite});
                else {
                    auto sites = graph.candidates(topology.crossings[i].region, id, width);
                    sites.erase(std::remove(sites.begin(), sites.end(), job.fixedSite), sites.end());
                    options.push_back(std::move(sites));
                }
                unavailable += options.back().empty();
            }
            if (topology.crossings.size() == 1) {
                // 单站点直接测实际局部布线；无须用束搜索近似同一锚点。
                for (int i = 0; i < options[0].size(); ++i) {
                    vector<int> one{options[0][i]};
                    if (!evaluated.insert(one).second) continue;
                    const double score = evaluate(topology, one);
                    if (score < bestScore) {
                        bestScore = score;
                        boundary = i >= (options[0].size() + 1) / 2;
                    }
                }
            } else {
                auto beam = beamSites(graph, topology, job, options, params, collisions);
                incomplete += beam.empty();
                for (const auto& state : beam) {
                    if (!evaluated.insert(state.sites).second) continue;
                    const double score = evaluate(topology, state.sites);
                    if (score < bestScore) {
                        bestScore = score;
                        boundary = state.boundary;
                    }
                }
            }
            // 无可布候选或真实成本最佳方案落在搜索后半区时扩张。
            if (width >= graph.policy.maxSites || (std::isfinite(bestScore) && !boundary)) break;
            width = std::min(2 * width, graph.policy.maxSites);
            ++expansions;
        }
    };
    const std::size_t limit = stage == Stage::Pattern ? 8 : 20;
    if (graph.policy.beam) {
        searchPositions(base);
        if (job.fixedSite < 0 && job.route.sites.size() == 1) evaluate(base, job.route.sites);
    } else {
        auto baseOptions = job.fixedSite >= 0 ? vector<int>{job.fixedSite}
                                             : graph.candidates(job.searchRegion, id, limit);
        unavailable += baseOptions.empty();
        if (job.fixedSite < 0 && job.route.sites.size() == 1 &&
            std::find(baseOptions.begin(), baseOptions.end(), job.route.sites.front()) == baseOptions.end()) {
            baseOptions.push_back(job.route.sites.front());
        }
        for (int site : baseOptions) evaluate(base, {site});
    }

    if (job.sides[Top]->getNumPins() + job.sides[Bottom]->getNumPins() <= 2) return finish();
    const auto tree = projectSteiner(job, graph);
    if (tree.empty()) return finish();
    vector<Topology> topologies;
    if (graph.policy.budgetDp && tree.size() <= graph.policy.maxNodes) {
        topologies = budgetTopologies(tree, job, graph, params);
        retained = topologies.size();
    } else {
        for (bool pinAffinity : {false, true}) topologies.push_back(labelTopology(tree, pinAffinity));
    }
    for (const auto& topology : topologies) {
        if (topology.crossings.empty() || (!graph.policy.budgetDp && topology.crossings.size() < 2) ||
            (!compareOverflow && graph.hbtCost * topology.crossings.size() >= best.cost)) continue;
        if (graph.policy.beam) {
            searchPositions(topology);
            continue;
        }
        vector<vector<int>> options;
        for (const auto& crossing : topology.crossings) {
            options.push_back(graph.candidates(crossing.region, id, limit));
            unavailable += options.back().empty();
        }
        const int fixedCrossing = fixedAnchor(topology);
        // Rank-diverse assignments bound the product of per-anchor candidates.
        // A candidate assignment may use each physical site at most once.
        for (std::size_t rank = 0; rank < (stage == Stage::Pattern ? 3u : 6u); ++rank) {
            vector<int> sites;
            for (int crossing = 0; crossing < options.size(); ++crossing) {
                if (crossing == fixedCrossing) {
                    sites.push_back(job.fixedSite);
                    continue;
                }
                const auto& candidates = options[crossing];
                if (candidates.empty() || rank >= candidates.size()) break;
                auto candidate = candidates.begin() + rank;
                while (candidate != candidates.end() &&
                       (*candidate == job.fixedSite ||
                        std::find(sites.begin(), sites.end(), *candidate) != sites.end())) ++candidate;
                if (candidate == candidates.end()) break;
                sites.push_back(*candidate);
            }
            if (sites.size() == options.size()) evaluate(topology, sites);
            else ++incomplete;
        }
    }
    return finish();
}

void routeJobs(F2fGraph& graph, vector<Job>& jobs, const Parameters& params) {
    PatternRoute::readFluteLUT();
    for (Stage stage : {Stage::Pattern, Stage::Detour, Stage::Maze}) {
        int routed = 0;
        int moved = 0;
        int restored = 0;
        std::array<GridGraphView<bool>, 2> congestion;
        if (stage == Stage::Detour) {
            for (int d = Top; d <= Bottom; ++d) graph.dies[d].graph().extractCongestionView(congestion[d]);
        }
        for (int i = 0; i < jobs.size(); ++i) {
            auto& job = jobs[i];
            if (graph.diagnostics.enabled() && stage == Stage::Pattern) {
                const auto projection = projectSteiner(job, graph);
                DBU length = 0;
                for (const auto& n : projection) for (int child : n.children) {
                    length += utils::Dist(n.point, projection[child].point);
                }
                const auto count = [&](int d) { return job.sides[d] ? job.sides[d]->getNumPins() : 0; };
                const auto name = [&](int d) { return job.sides[d] ? job.sides[d]->getName() : std::string(); };
                graph.diagnostics.write({{"type", "net"}, {"net", i}, {"top", name(Top)}, {"bottom", name(Bottom)},
                    {"pins", {count(Top), count(Bottom)}}, {"projected_rsmt_um",
                    projection.empty() ? Json() : Json(double(length) / graph.packageDbu)}});
            }
            if (stage != Stage::Pattern) {
                bool overflow = false;
                for (int d = Top; d <= Bottom; ++d) {
                    for (const auto& component : job.route.components[d]) {
                        overflow |= graph.dies[d].graph().hasResourceOverflow(component.getRoutingTree());
                    }
                }
                if (!overflow) {
                    if (graph.diagnostics.enabled()) {
                        graph.diagnostics.write({{"type", "decision"}, {"net", i}, {"stage", phase(stage)},
                            {"action", "untouched"}, {"sites", job.route.sites}});
                    }
                    continue;
                }
                graph.apply(i, job.route, true);
            }
            RoutedNet plan;
            bool keepOld = false;
            try {
                plan = planJob(graph, job, i, params, stage, congestion);
                if (stage != Stage::Pattern && graph.policy.compareOld && std::isfinite(plan.cost)) {
                    // 旧路由缓存的代价来自上一次求解，必须在旧资源已释放时重新估算。
                    job.route.cost = graph.hbtCost * job.route.sites.size();
                    for (int d = Top; d <= Bottom; ++d) {
                        job.route.cost += graph.dies[d].graph().getForestCost(
                            job.route.components[d], graph.policy.uniqueSteps);
                    }
                    measureReroute(job.route, graph);
                    keepOld = !improves(plan, job.route, 1e-8);
                }
            } catch (...) {
                if (stage != Stage::Pattern) graph.apply(i, job.route, false);
                throw;
            }
            if (!std::isfinite(plan.cost)) {
                if (graph.diagnostics.enabled()) {
                    graph.diagnostics.write({{"type", "decision"}, {"net", i}, {"stage", phase(stage)},
                        {"action", stage == Stage::Pattern ? "failed" : "restore_infeasible"},
                        {"sites", job.route.sites}});
                }
                if (stage != Stage::Pattern) {
                    graph.apply(i, job.route, false);
                    ++restored;
                    continue;
                }
                const auto& net = job.sides[Top] ? job.sides[Top] : job.sides[Bottom];
                throw std::runtime_error("No feasible route for " + net->getName());
            }
            if (graph.diagnostics.enabled()) {
                Json event = {{"type", "decision"}, {"net", i}, {"stage", phase(stage)},
                              {"action", keepOld ? "restore_no_gain" : "accept"},
                              {"sites", keepOld ? job.route.sites : plan.sites}, {"new", routeDiagnostics(plan, graph)}};
                if (stage != Stage::Pattern) event["old"] = routeDiagnostics(job.route, graph);
                graph.diagnostics.write(event);
            }
            if (keepOld) {
                graph.apply(i, job.route, false);
                ++restored;
                continue;
            }
            if (!job.route.sites.empty() && job.route.sites != plan.sites) ++moved;
            job.route = std::move(plan);
            graph.apply(i, job.route, false);
            ++routed;
        }
        log() << "F2F routing stage " << static_cast<int>(stage) + 1 << ": "
              << routed << " routes committed, " << moved << " nets changed HBT assignments, "
              << restored << " old routes restored\n";
        if (graph.diagnostics.enabled()) {
            Json dies = Json::array();
            for (auto& die : graph.dies) {
                const auto& grid = die.graph();
                dies.push_back({{"wire_um", double(grid.getCommittedWireLength()) / grid.getLibDBU()},
                                {"vias", grid.getCommittedViaCount()}, {"wire_overflow", grid.getTotalWireOverflow()},
                                {"max_wire_overflow", grid.getMaxWireOverflow()}});
            }
            graph.diagnostics.write({{"type", "stage"}, {"stage", phase(stage)}, {"dies", dies}});
        }
    }
}

void writeOutputs(F2fGraph& graph, const vector<Job>& jobs, const std::string& hbtFile,
                  const std::string& paretoFile) {
    if (graph.dies[Top].guideFile == graph.dies[Bottom].guideFile ||
        graph.dies[Top].guideFile == hbtFile || graph.dies[Bottom].guideFile == hbtFile ||
        (!paretoFile.empty() && (paretoFile == graph.dies[Top].guideFile ||
                                 paretoFile == graph.dies[Bottom].guideFile ||
                                 paretoFile == hbtFile))) {
        throw std::runtime_error("F2F outputs must have distinct paths");
    }
    std::array<std::ofstream, 2> guides{{std::ofstream(graph.dies[Top].guideFile),
                                        std::ofstream(graph.dies[Bottom].guideFile)}};
    std::ofstream bonds(hbtFile);
    std::ofstream pareto;
    if (!paretoFile.empty()) {
        pareto.open(paretoFile);
        if (!pareto) throw std::runtime_error("Cannot open F2F Pareto output: " + paretoFile);
        pareto << "net_id\ttop_net\tbottom_net\tstage\trank\thbt_count\twirelength_um\tvia_count"
                  "\twire_overflow_delta\tresource_overflow_delta\tcongestion_cost\tsite_ids\n";
    }
    if (!guides[Top] || !guides[Bottom] || !bonds) throw std::runtime_error("Cannot open F2F outputs");
    bonds << "net_id\ttop_net\tbottom_net\tsite_id\tpackage_x\tpackage_y\tpackage_dbu";
    for (const auto* side : {"top", "bottom"}) {
        bonds << '\t' << side << "_layer\t" << side << "_gcell_x\t" << side << "_gcell_y"
              << '\t' << side << "_local_x_package_dbu\t" << side << "_local_y_package_dbu";
    }
    bonds << "\tfixed\n";
    int assigned = 0;
    int overflows = 0;
    for (int i = 0; i < jobs.size(); ++i) {
        const auto& job = jobs[i];
        for (int d = Top; d <= Bottom; ++d) {
            if (!job.sides[d]) continue;
            auto& die = graph.dies[d];
            vector<GRPoint> landings;
            for (int site : job.route.sites) landings.push_back(graph.sites[site].ends[d]);
            const auto& components = job.route.components[d];
            die.router.writeNetGuides(guides[d], job.sides[d]->getName(), components, landings);
            for (const auto& component : components) {
                overflows += die.graph().hasResourceOverflow(component.getRoutingTree());
            }
        }
        for (int index : job.route.sites) {
            const auto& hbt = graph.sites.at(index);
            if (hbt.owner != i) throw std::runtime_error("Missing committed HBT owner");
            ++assigned;
            bonds << i << '\t' << job.sides[Top]->getName() << '\t' << job.sides[Bottom]->getName() << '\t'
                  << index << '\t' << hbt.position.x << '\t' << hbt.position.y << '\t' << graph.packageDbu;
            for (int d = Top; d <= Bottom; ++d) {
                const auto& landing = hbt.ends[d];
                const auto local = graph.dies[d].localPackageUnits(hbt.position);
                bonds << '\t' << graph.dies[d].graph().getLayerName(landing.layerIdx)
                      << '\t' << landing.x << '\t' << landing.y << '\t' << local.x << '\t' << local.y;
            }
            bonds << '\t' << hbt.fixed << '\n';
        }
        if (pareto) {
            const auto topName = job.sides[Top] ? job.sides[Top]->getName() : std::string();
            const auto bottomName = job.sides[Bottom] ? job.sides[Bottom]->getName() : std::string();
            for (std::size_t rank = 0; rank < job.paretoArchive.size(); ++rank) {
                const auto& candidate = job.paretoArchive[rank];
                const auto& m = candidate.metrics;
                pareto << i << '\t' << topName << '\t' << bottomName << '\t'
                       << candidate.stage << '\t' << rank << '\t' << m.hbtCount << '\t'
                       << m.wireLengthUm << '\t' << m.viaCount << '\t'
                       << m.wireOverflowDelta << '\t' << m.resourceOverflowDelta << '\t'
                       << m.congestionCost << '\t';
                for (std::size_t site = 0; site < candidate.sites.size(); ++site) {
                    if (site) pareto << ',';
                    pareto << candidate.sites[site];
                }
                pareto << '\n';
            }
        }
    }
    for (auto& guide : guides) guide.close();
    bonds.close();
    if (pareto) pareto.close();
    if (!guides[Top] || !guides[Bottom] || !bonds || (!paretoFile.empty() && !pareto)) {
        throw std::runtime_error("Cannot write F2F outputs");
    }
    log() << "Assigned HBTs: " << assigned << ", overflowing die-local routes: " << overflows << '\n';
}

} // namespace

void runF2f(const Parameters& parameters) {
    std::ifstream file(parameters.f2f_config);
    if (!file) throw std::runtime_error("Cannot open F2F configuration: " + parameters.f2f_config);
    Json config;
    file >> config;
    const auto paretoFile = config.value("pareto_output", std::string());
    if (!paretoFile.empty()) {
        const auto output = boost::filesystem::absolute(paretoFile).lexically_normal();
        std::vector<std::string> protectedPaths = {
            config.at("top").at("lef").get<std::string>(),
            config.at("top").at("def").get<std::string>(),
            config.at("top").at("guide").get<std::string>(),
            config.at("bottom").at("lef").get<std::string>(),
            config.at("bottom").at("def").get<std::string>(),
            config.at("bottom").at("guide").get<std::string>(),
            config.at("hbt_output").get<std::string>()
        };
        const auto diagnosticsPath = config.value("diagnostics", std::string());
        if (!diagnosticsPath.empty()) protectedPaths.push_back(diagnosticsPath);
        for (const auto& protectedPath : protectedPaths) {
            if (output == boost::filesystem::absolute(protectedPath).lexically_normal()) {
                throw std::runtime_error("F2F Pareto output must not overwrite an input or another output");
            }
        }
    }
    F2fGraph graph(parameters, config);
    auto jobs = buildJobs(graph, config);
    log() << "F2F nets: " << jobs.size() << ", legal HBT sites: " << graph.sites.size() << '\n';
    const auto start = std::chrono::steady_clock::now();
    routeJobs(graph, jobs, parameters);
    const double routeSeconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    writeOutputs(graph, jobs, config.at("hbt_output").get<std::string>(), paretoFile);
    double wireMetric = 0, wirelengthUm = 0, overflow = 0;
    int64_t vias = 0, bonds = 0;
    for (auto& die : graph.dies) {
        const auto& grid = die.graph();
        wireMetric += double(grid.getCommittedWireLength()) / grid.getM2Pitch();
        wirelengthUm += double(grid.getCommittedWireLength()) / grid.getLibDBU();
        vias += grid.getCommittedViaCount();
        overflow += grid.getTotalWireOverflow();
    }
    for (const auto& job : jobs) bonds += job.route.sites.size();
    log() << "F2F_METRICS " << Json({{"wire_length_metric", wireMetric},
        {"wirelength_um", wirelengthUm}, {"via_count", vias}, {"wire_overflow", overflow},
        {"used_hbt", bonds}, {"router_reported_seconds", routeSeconds}}).dump() << '\n';
}
