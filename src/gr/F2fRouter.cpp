#include "F2fRouter.h"
#include "GlobalRouter.h"
#include "MazeRoute.h"
#include "PatternRoute.h"
#include <array>
#include <chrono>
#include <map>

namespace {

using XY = utils::PointT<DBU>;
using Box = utils::BoxT<DBU>;
using Json = Rsyn::Json;
enum Die { Top, Bottom };

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

    GRPoint landing(const XY& position) {
        const auto local = localPackageUnits(position);
        int cells[2];
        for (int axis = 0; axis < 2; ++axis) {
            const auto& lines = design.getGridlines()[axis];
            auto it = std::upper_bound(lines.begin(), lines.end(), local[axis],
                [this](DBU value, DBU line) { return value < line * scale; });
            cells[axis] = std::min<int>(it - lines.begin() - 1, lines.size() - 2);
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
};

struct Job {
    std::array<std::unique_ptr<GRNet>, 2> sides;
    RoutedNet route;
    int fixedSite = -1;
    Box searchRegion;

    bool crosses() const { return sides[Top] && sides[Bottom]; }
};

class F2fGraph {
public:
    std::array<DieGrid, 2> dies;
    vector<HbtEdge> sites;
    CostT hbtCost;
    DBU packageDbu;
    RTree siteIndex;

    F2fGraph(const Parameters& params, const Json& config)
        : dies{{DieGrid(params, config.at("top"), config.at("package_dbu").get<DBU>()),
                DieGrid(params, config.at("bottom"), config.at("package_dbu").get<DBU>())}},
          hbtCost(config.at("hbt").value("cost", 4.0)),
          packageDbu(config.at("package_dbu").get<DBU>()) {
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
            dies[die].graph().commitForest(route.components[die], reverse);
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
        if (hasPins[i][1 - d]) {
            opposite = component(1 - d);
            cross(current, opposite, Box(n.point.x, n.point.y, n.point.x, n.point.y));
        }
        for (const auto& terminal : n.terminals) {
            topology.components[terminal.die == d ? current : opposite].pins.push_back(terminal.pin);
        }
        for (int child : n.children) {
            if (costs[child][d] <= 1 + costs[child][1 - d]) {
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
    const int rootDie = costs[0][Top] <= costs[0][Bottom] ? Top : Bottom;
    visit(visit, 0, rootDie, component(rootDie));
    return topology;
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
    for (int d = Top; d <= Bottom; ++d) result.cost += graph.dies[d].graph().getForestCost(result.components[d]);
    return result;
}

RoutedNet planJob(F2fGraph& graph, const Job& job, int id, const Parameters& params, Stage stage,
                  const std::array<GridGraphView<bool>, 2>& congestion) {
    RoutedNet best;
    auto evaluate = [&](const Topology& topology, const vector<int>& sites) {
        auto plan = evaluateTopology(topology, sites, job, graph, params, stage, congestion);
        if (plan.cost < best.cost || (plan.cost == best.cost && sites.size() < best.sites.size())) {
            best = std::move(plan);
        }
    };
    const auto base = oneCrossing(job);
    if (!job.crosses()) {
        evaluate(base, {});
        return best;
    }
    const std::size_t limit = stage == Stage::Pattern ? 8 : 20;
    auto baseOptions = job.fixedSite >= 0 ? vector<int>{job.fixedSite}
                                         : graph.candidates(job.searchRegion, id, limit);
    if (job.fixedSite < 0 && job.route.sites.size() == 1 &&
        std::find(baseOptions.begin(), baseOptions.end(), job.route.sites.front()) == baseOptions.end()) {
        baseOptions.push_back(job.route.sites.front());
    }
    for (int site : baseOptions) evaluate(base, {site});

    if (job.sides[Top]->getNumPins() + job.sides[Bottom]->getNumPins() <= 2) return best;
    const auto tree = projectSteiner(job, graph);
    if (tree.empty()) return best;
    for (bool pinAffinity : {false, true}) {
        auto topology = labelTopology(tree, pinAffinity);
        if (topology.crossings.size() < 2 || graph.hbtCost * topology.crossings.size() >= best.cost) continue;
        vector<vector<int>> options;
        for (const auto& crossing : topology.crossings) {
            options.push_back(graph.candidates(crossing.region, id, limit));
        }
        int fixedCrossing = -1;
        if (job.fixedSite >= 0) {
            const auto& p = graph.sites[job.fixedSite].position;
            auto distance = [&](const Crossing& edge) {
                const auto& b = edge.region;
                return std::max({DBU(0), b.x.low - p.x, p.x - b.x.high}) +
                       std::max({DBU(0), b.y.low - p.y, p.y - b.y.high});
            };
            fixedCrossing = std::min_element(topology.crossings.begin(), topology.crossings.end(),
                [&](const Crossing& a, const Crossing& b) { return distance(a) < distance(b); }) -
                topology.crossings.begin();
        }
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
        }
    }
    return best;
}

void routeJobs(F2fGraph& graph, vector<Job>& jobs, const Parameters& params) {
    PatternRoute::readFluteLUT();
    for (Stage stage : {Stage::Pattern, Stage::Detour, Stage::Maze}) {
        int routed = 0;
        int moved = 0;
        std::array<GridGraphView<bool>, 2> congestion;
        if (stage == Stage::Detour) {
            for (int d = Top; d <= Bottom; ++d) graph.dies[d].graph().extractCongestionView(congestion[d]);
        }
        for (int i = 0; i < jobs.size(); ++i) {
            auto& job = jobs[i];
            if (stage != Stage::Pattern) {
                bool overflow = false;
                for (int d = Top; d <= Bottom; ++d) {
                    for (const auto& component : job.route.components[d]) {
                        overflow |= graph.dies[d].graph().hasResourceOverflow(component.getRoutingTree());
                    }
                }
                if (!overflow) continue;
                graph.apply(i, job.route, true);
            }
            RoutedNet plan;
            try {
                plan = planJob(graph, job, i, params, stage, congestion);
            } catch (...) {
                if (stage != Stage::Pattern) graph.apply(i, job.route, false);
                throw;
            }
            if (!std::isfinite(plan.cost)) {
                if (stage != Stage::Pattern) {
                    graph.apply(i, job.route, false);
                    continue;
                }
                const auto& net = job.sides[Top] ? job.sides[Top] : job.sides[Bottom];
                throw std::runtime_error("No feasible route for " + net->getName());
            }
            if (!job.route.sites.empty() && job.route.sites != plan.sites) ++moved;
            job.route = std::move(plan);
            graph.apply(i, job.route, false);
            ++routed;
        }
        log() << "F2F routing stage " << static_cast<int>(stage) + 1 << ": "
              << routed << " routes committed, " << moved << " nets changed HBT assignments\n";
    }
}

void writeOutputs(F2fGraph& graph, const vector<Job>& jobs, const std::string& hbtFile) {
    if (graph.dies[Top].guideFile == graph.dies[Bottom].guideFile ||
        graph.dies[Top].guideFile == hbtFile || graph.dies[Bottom].guideFile == hbtFile) {
        throw std::runtime_error("F2F outputs must have distinct paths");
    }
    std::array<std::ofstream, 2> guides{{std::ofstream(graph.dies[Top].guideFile),
                                        std::ofstream(graph.dies[Bottom].guideFile)}};
    std::ofstream bonds(hbtFile);
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
    }
    for (auto& guide : guides) guide.close();
    bonds.close();
    if (!guides[Top] || !guides[Bottom] || !bonds) throw std::runtime_error("Cannot write F2F outputs");
    log() << "Assigned HBTs: " << assigned << ", overflowing die-local routes: " << overflows << '\n';
}

} // namespace

void runF2f(const Parameters& parameters) {
    std::ifstream file(parameters.f2f_config);
    if (!file) throw std::runtime_error("Cannot open F2F configuration: " + parameters.f2f_config);
    Json config;
    file >> config;
    F2fGraph graph(parameters, config);
    auto jobs = buildJobs(graph, config);
    log() << "F2F nets: " << jobs.size() << ", legal HBT sites: " << graph.sites.size() << '\n';
    const auto start = std::chrono::steady_clock::now();
    routeJobs(graph, jobs, parameters);
    const double routeSeconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    writeOutputs(graph, jobs, config.at("hbt_output").get<std::string>());
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
