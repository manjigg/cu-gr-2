#include "H3DGlobalRouter.h"
#include "H3DDesign.h"
#include "H3DWriter.h"
#include "DieAssignment.h"
#include "HbtLocationOptimizer.h"
#include "SubnetDecomposer.h"
#include "H3DDiagnostics.h"
#include <boost/filesystem.hpp>
#include <set>
#include <stdexcept>
#include <queue>
#include <chrono>
#include <random>

namespace h3d {
namespace {
using Clock = std::chrono::steady_clock;
struct PhaseTimer {
    std::map<std::string, double>& times;
    std::string phase;
    Clock::time_point start = Clock::now();
    PhaseTimer(std::map<std::string, double>& values, std::string name): times(values), phase(std::move(name)) {}
    ~PhaseTimer() { times[phase] += std::chrono::duration<double>(Clock::now() - start).count(); }
};

Rsyn::Json paperLocationJson(const LocationResult& result) {
    Rsyn::Json value = {{"roots", result.paperRoots}, {"bfs_order", result.paperBfsOrder},
                        {"directional_ties", "all_nearest"}, {"nodes", Rsyn::Json::array()},
                         {"continuous_positions_dbu", Rsyn::Json::array()}, {"continuous_wirelength_dbu", result.continuousWirelength}};
    value["traceback_tie_policy"] = result.paperTiePolicy;
    value["common_site_trials"] = result.paperTieTrials;
    value["sites_before_tie_break"] = result.paperSitesBeforeTieBreak;
    value["wirelength_before_tie_break_dbu"] = result.paperWirelengthBeforeTieBreak;
    value["pair_group_trials"] = result.paperPairTieTrials;
    value["tie_break_revision"] = "common_then_pair_groups_v2";
    value["tie_merges"] = Rsyn::Json::array();
    for (const auto& merge : result.paperTieMerges) value["tie_merges"].push_back({
        {"forced_nodes", merge.forcedNodes}, {"site", merge.site}, {"selected", merge.selected}});
    for (auto point : result.continuousPositions) value["continuous_positions_dbu"].push_back({point.x, point.y});
    for (const auto& node : result.paperNodes) {
        value["nodes"].push_back({{"node", node.node}, {"domain", node.hbt ? "hbt" : "routing"}, {"children", node.children},
            {"region", {node.region.lx(), node.region.ly(), node.region.hx(), node.region.hy()}},
            {"candidate_sites", node.candidateSites}, {"costs", node.costs}, {"child_choices", node.childChoices}, {"selected", node.selected}});
    }
    return value;
}

Rsyn::Json topologyJson(const H3DTree& tree) {
    Rsyn::Json value = {{"nodes", Rsyn::Json::array()}, {"edges", Rsyn::Json::array()}};
    for (const auto& node : tree.getNodes()) {
        std::vector<int> dies;
        for (const auto& pin : node.pins) dies.push_back(static_cast<int>(pin.die));
        value["nodes"].push_back({{"position_dbu", {node.position.x, node.position.y}}, {"pin_dies", dies}});
    }
    for (const auto& edge : tree.getEdges()) value["edges"].push_back({edge.u, edge.v});
    return value;
}

Rsyn::Json routingSnapshot(const H3DDesign& design) {
    Rsyn::Json result;
    for (int die = 0; die < 2; die++) {
        std::string label = die == 0 ? "bottom" : "top";
        int overflowNets = 0;
        DBU wirelength = 0;
        std::int64_t vias = 0;
        const auto& graph = design.dies[die].router->getGridGraph();
        std::map<std::tuple<int, int, int>, int> usage;
        for (const auto& net : design.dies[die].router->getNets()) {
            if (!net.getRoutingTree()) continue;
            if (graph.checkOverflow(net.getRoutingTree()) > 0) overflowNets++;
            GRTreeNode::preorder(net.getRoutingTree(), [&](std::shared_ptr<GRTreeNode> node) {
                for (const auto& child : node->children) {
                    if (node->layerIdx != child->layerIdx) { vias += std::abs(node->layerIdx - child->layerIdx); continue; }
                    wirelength += utils::Dist(cellCenter(graph, *node), cellCenter(graph, *child));
                    auto direction = graph.getLayerDirection(node->layerIdx);
                    int low = std::min((*node)[direction], (*child)[direction]);
                    int high = std::max((*node)[direction], (*child)[direction]);
                    for (int i = low; i < high; i++) {
                        int x = direction == MetalLayer::H ? i : node->x;
                        int y = direction == MetalLayer::H ? node->y : i;
                        usage[{node->layerIdx, x, y}]++;
                    }
                }
            });
        }
        double wireOverflow = 0, demandOverflow = 0;
        for (const auto& item : usage) {
            int layer, x, y; std::tie(layer, x, y) = item.first;
            wireOverflow += std::max(0.0, item.second - std::max(0.0, graph.getEdge(layer, x, y).capacity));
        }
        for (int layer = 1; layer < graph.getNumLayers(); layer++) {
            auto direction = graph.getLayerDirection(layer);
            for (int x = 0; x < graph.getSize(0) - 1 + direction; x++) {
                for (int y = 0; y < graph.getSize(1) - direction; y++) demandOverflow += std::max(0.0, -graph.getEdge(layer, x, y).getResource());
            }
        }
        result[label] = {{"subnet_count", design.dies[die].router->getNets().size()}, {"overflow_net_count", overflowNets},
                         {"wirelength_dbu", wirelength}, {"via_count", vias}, {"wire_overflow_tracks", wireOverflow},
                         {"demand_overflow_tracks", demandOverflow}};
    }
    return result;
}

bool covers(const std::shared_ptr<GRTreeNode>& tree, const GRPoint& point) {
    bool found = false;
    GRTreeNode::preorder(tree, [&](std::shared_ptr<GRTreeNode> node) {
        if (node->layerIdx == point.layerIdx && node->x == point.x && node->y == point.y) found = true;
        for (const auto& child : node->children) {
            if (node->layerIdx == child->layerIdx) {
                if (node->layerIdx != point.layerIdx) continue;
                if (node->y == child->y && node->y == point.y && point.x >= std::min(node->x, child->x) && point.x <= std::max(node->x, child->x)) found = true;
                if (node->x == child->x && node->x == point.x && point.y >= std::min(node->y, child->y) && point.y <= std::max(node->y, child->y)) found = true;
            } else {
                if (node->x != child->x || node->y != child->y) throw std::runtime_error("A routing via changes x/y");
                if (node->x == point.x && node->y == point.y && point.layerIdx >= std::min(node->layerIdx, child->layerIdx) &&
                    point.layerIdx <= std::max(node->layerIdx, child->layerIdx)) found = true;
            }
        }
    });
    return found;
}

void validate(const H3DDesign& design, const std::vector<Subnet>& subnets, const std::set<int>& selected) {
    using Key = std::tuple<int, int, int>;
    std::map<Key, int> originalPins, seen;
    for (const auto& net : design.nets) if (selected.count(net.id)) for (const auto& pin : net.pins) {
        if (!originalPins.emplace(Key(static_cast<int>(pin.die), pin.instanceIndex, pin.pinIndex), net.id).second) {
            throw std::runtime_error("Original terminal occurs on multiple nets");
        }
    }
    std::map<int, std::array<int, 2>> siteUses;
    std::map<int, std::vector<const Subnet*>> groupedSubnets;
    for (const auto& subnet : subnets) {
        groupedSubnets[subnet.original].push_back(&subnet);
        const auto& routed = design.dies[subnet.die].router->getNets().at(subnet.routingIndex);
        // A terminal is a physical pin, not one arbitrary projection chosen for
        // the original multi-die tree. Subnet routing may choose another legal
        // access cell on that same pin after the split; every pin must still be
        // reached on an actual shape/layer candidate.
        if (routed.getPinAccessPoints().size() != subnet.pins.size()) {
            throw std::runtime_error("Routing terminal count differs from subnet pins");
        }
        for (const auto& access : routed.getPinAccessPoints()) {
            bool reached = false;
            for (const auto& point : access) reached |= covers(routed.getRoutingTree(), point);
            if (!reached) throw std::runtime_error("Routing tree misses a physical terminal: " + subnet.name);
        }
        for (const auto& pin : subnet.pins) {
            Key key(subnet.die, pin.instanceIndex, pin.pinIndex);
            auto found = originalPins.find(key);
            if (found != originalPins.end()) {
                if (found->second != subnet.original) throw std::runtime_error("Terminal moved to another original net");
                seen[key]++;
            }
        }
        for (int site : subnet.hbtSites) {
            if (design.hbt->owner(site) != subnet.original) throw std::runtime_error("HBT ownership mismatch");
            siteUses[site][subnet.die]++;
        }
    }
    for (const auto& pin : originalPins) if (seen[pin.first] != 1) throw std::runtime_error("Original terminal lost or duplicated");
    for (const auto& site : siteUses) if (site.second[0] != 1 || site.second[1] != 1) throw std::runtime_error("HBT must have exactly one terminal on each die");
    // 每条原始网：子网通过相同 HBT 连接后必须形成一个连通分量。
    for (const auto& net : design.nets) {
        if (!selected.count(net.id)) continue;
        const auto& local = groupedSubnets[net.id];
        std::map<int, std::vector<std::size_t>> incident;
        for (std::size_t i = 0; i < local.size(); i++) for (int site : local[i]->hbtSites) incident[site].push_back(i);
        std::set<int> expandedSites;
        std::set<std::size_t> reached;
        std::queue<std::size_t> queue;
        if (!local.empty()) { reached.insert(0); queue.push(0); }
        while (!queue.empty()) {
            auto index = queue.front(); queue.pop();
            for (int site : local[index]->hbtSites) if (expandedSites.insert(site).second) {
                for (auto next : incident[site]) if (reached.insert(next).second) queue.push(next);
            }
        }
        if (local.empty() || reached.size() != local.size()) throw std::runtime_error("Disconnected cross-die net: " + net.name);
    }
}
} // namespace

void routeH3D(const Parameters& parameters) {
    const auto totalStart = Clock::now();
    const bool paper = parameters.h3d_method == "paper";
    std::mt19937 paperRandom(parameters.h3d_seed);
    boost::filesystem::create_directories(parameters.output_dir);
    auto initializationStart = std::chrono::steady_clock::now();
    H3DDesign design(parameters);
    const std::string subnetTopology = design.config.value("subnet_routing_topology",
        std::string(paper ? "flute" : "inherited"));
    if (subnetTopology != "flute" && subnetTopology != "inherited") {
        throw std::invalid_argument("subnet_routing_topology must be flute or inherited");
    }
    design.config["subnet_routing_topology"] = subnetTopology;
    const std::string subnetAccess = design.config.value("subnet_pin_access",
        std::string(subnetTopology == "flute" ? "physical_shapes" : "fixed_projection"));
    if (subnetAccess != "physical_shapes" && subnetAccess != "fixed_projection") {
        throw std::invalid_argument("subnet_pin_access must be physical_shapes or fixed_projection");
    }
    design.config["subnet_pin_access"] = subnetAccess;
    const bool fullTrace = design.config.value("write_routing_traces", true);
    double initializationSeconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - initializationStart).count();
    if (runH3DDiagnostics(parameters, design, initializationSeconds)) return;
    std::map<std::string, double> phaseSeconds = {{"initialization", initializationSeconds}};
    std::set<std::string> requested;
    if (!parameters.h3d_net_selection_file.empty()) {
        std::ifstream input(parameters.h3d_net_selection_file);
        if (!input) throw std::runtime_error("Cannot read routing net selection");
        Rsyn::Json names; input >> names;
        for (const auto& item : names) if (!requested.insert(item.get<std::string>()).second) throw std::invalid_argument("Duplicate routing net selection");
        if (requested.empty()) throw std::invalid_argument("Routing net selection cannot be empty");
    }
    std::set<int> selected;
    std::set<std::string> found;
    for (const auto& net : design.nets) if (requested.empty() || requested.count(net.name)) { selected.insert(net.id); found.insert(net.name); }
    if (!requested.empty() && requested != found) throw std::invalid_argument("Routing selection includes an unknown original net");
    Rsyn::Json report;
    report["schema_version"] = 1;
    report["method_profile"] = parameters.h3d_method;
    report["random_seed"] = parameters.h3d_seed;
    report["paper_traceback_ties"] = parameters.h3d_paper_ties;
    report["paper_assignment_trial_limit"] = parameters.h3d_assignment_trials;
    report["routing_observations"] = fullTrace ? "full_per_net_trace" : "summary_without_dp_trace";
    report["method_features"] = {{"continuous_regions_and_eight_directions", paper},
        {"all_directional_nearest_ties", paper}, {"seeded_leaf_and_variable_roots", paper},
        {"full_domain_distance_transform", !paper}, {"future_site_reservation", !paper},
        {"forced_single_site_budget_fallback", !paper}, {"reverse_dfs_hbt_split", paper}};
    report["method_features"]["zero_cost_common_site_tie_break"] = paper && parameters.h3d_paper_ties == "coalesce";
    report["method_features"]["quantity_dp_equivalent_assignment_search"] = paper && parameters.h3d_assignment_trials > 1;
    report["method_features"]["rebuild_single_die_subnet_rsmt"] = subnetTopology == "flute";
    report["method_features"]["subnet_physical_pin_access"] = subnetAccess == "physical_shapes";
    report["dbu_per_micron"] = design.dies[0].design->getLibDBU();
    report["original_net_count"] = design.nets.size();
    report["experiment_scope"] = requested.empty() ? "full_design" : "explicit_net_subset_with_remaining_source_nets_preserved";
    report["selected_net_count"] = selected.size();
    report["physical_configuration"] = design.config;
    report["completed_input_nets"] = 0;
    int remainingCross = 0;
    for (const auto& net : design.nets) if (selected.count(net.id) && net.crossesDies()) remainingCross++;
    report["selected_cross_die_net_count"] = remainingCross;
    report["hbt_capacity_policy"] = paper ? "sequential_currently_available_sites_only" : "reserve_one_site_per_remaining_cross_die_net";
    report["initial_available_hbt_sites"] = design.hbt->available();
    report["hbt_resource_audit"] = {{"cross_die_net_lower_bound", remainingCross},
        {"initial_legal_sites", design.hbt->available()}, {"headroom_above_one_site_per_cross_die_net", design.hbt->available() - remainingCross},
        {"scope", "counts and commitment diagnostics only; does not reserve sites or change paper decisions"}};
    report["nets"] = Rsyn::Json::array();
    Rsyn::Json grid = {{"sites", Rsyn::Json::array()}};
    for (int i = 0; i < design.hbt->size(); i++) {
        auto point = design.hbt->point(i);
        auto physicalPoint = design.physicalHbtPoint(i);
        grid["sites"].push_back({{"id", i}, {"position_dbu", {point.x, point.y}},
            {"physical_position_dbu", {physicalPoint.x, physicalPoint.y}}, {"initial_owner", design.hbt->owner(i)}});
    }
    std::ofstream gridFile((boost::filesystem::path(parameters.output_dir) / "hbt_grid_initial.json").string());
    gridFile << grid.dump(2) << '\n'; gridFile.close();
    std::ofstream traceFile;
    if (fullTrace) {
        traceFile.open((boost::filesystem::path(parameters.output_dir) / "routing_traces.jsonl").string());
        if (!traceFile) throw std::runtime_error("Cannot write per-net routing observations");
    }
    std::vector<Subnet> subnets;
    int crossCount = 0, topologyHbtCount = 0;
    std::string activeStage, activeNet;
    Rsyn::Json observation;
    try {
    activeStage = "hbt_capacity_preflight";
    if (!paper && design.hbt->available() < remainingCross) {
        throw std::runtime_error("Insufficient HBT sites for the cross-die-net lower bound");
    }
    for (const auto& net : design.nets) {
        if (!selected.count(net.id)) continue;
        activeNet = net.name;
        if (fullTrace) {
            observation = {{"name", net.name}, {"id", net.id}, {"endpoint_count", net.pins.size()}};
            log() << "H3D begin " << net.name << " pins=" << net.pins.size() << std::endl;
        }
        H3DTree planar;
        DieAssignmentResult assignment;
        AssignedTopology expanded;
        activeStage = "planar_rsmt";
        { PhaseTimer timer(phaseSeconds, activeStage); planar = design.planarTree(net); }
        activeStage = "hbt_count_dp";
        int assignmentRoot = -1;
        if (paper && net.crossesDies()) assignmentRoot = chooseLeafRoot(planar, paperRandom);
        if (paper && !net.crossesDies()) {
            // III-A/E：单 die 网不执行跨 die 数量优化，直接保留该 die 的 FLUTE 拓扑。
            auto die = net.pins.front().die;
            assignment.root = planar.rootAt().root;
            assignment.edgeDies.assign(planar.getEdges().size(), die);
            assignment.nodeDieSpans.assign(planar.getNodes().size(), {die, die});
        } else {
            PhaseTimer timer(phaseSeconds, activeStage);
            assignment = DieAssignment().run(planar, assignmentRoot);
        }
        expanded = expandAssignment(planar, assignment);
        if (fullTrace) {
            observation["method_profile"] = parameters.h3d_method;
            observation["die_assignment_root"] = assignment.root;
            observation["hbt_count_dp_applied"] = !paper || net.crossesDies();
            observation["planar"] = topologyJson(planar);
            observation["hbt_count"] = assignment.hbtCount;
            observation["edge_dies"] = Rsyn::Json::array();
            for (auto die : assignment.edgeDies) observation["edge_dies"].push_back(static_cast<int>(die));
            observation["expanded"] = topologyJson(expanded.tree);
            observation["expanded_edge_dies"] = Rsyn::Json::array();
            for (auto die : expanded.edgeDies) observation["expanded_edge_dies"].push_back(static_cast<int>(die));
            observation["hbt_nodes"] = expanded.hbtNodes;
        }
        LocationResult locations;
        if (net.crossesDies()) {
            crossCount++;
            if (fullTrace) observation["hbt_free_sites_before"] = design.hbt->available();
            activeStage = "hbt_location_dp";
            const int futureReserve = remainingCross - 1;
            if (paper) {
                auto seed = static_cast<std::uint32_t>(paperRandom());
                if (fullTrace) observation["location_seed"] = seed;
                { PhaseTimer timer(phaseSeconds, activeStage);
                    auto solution = HbtLocationOptimizer().runPaperAssignments(planar, assignment, *design.hbt, *design.routing, seed,
                        parameters.h3d_paper_ties == "coalesce", parameters.h3d_assignment_trials);
                   assignment = std::move(solution.assignment); expanded = std::move(solution.expanded); locations = std::move(solution.location);
                   if (fullTrace) {
                        observation["paper_assignment_trials"] = Rsyn::Json::array();
                        observation["selected_assignment_trial"] = solution.selectedTrial;
                        for (const auto& trial : solution.trials) {
                            Rsyn::Json edgeDies = Rsyn::Json::array();
                            for (auto die : trial.edgeDies) edgeDies.push_back(static_cast<int>(die));
                            observation["paper_assignment_trials"].push_back({{"tie_seed", trial.seed}, {"edge_dies", edgeDies},
                                {"hbt_nodes", trial.hbtNodes}, {"physical_sites", trial.physicalSites}, {"wirelength_dbu", trial.wirelength}});
                        }
                        observation["edge_dies"] = Rsyn::Json::array();
                        for (auto die : assignment.edgeDies) observation["edge_dies"].push_back(static_cast<int>(die));
                        observation["expanded"] = topologyJson(expanded.tree);
                        observation["expanded_edge_dies"] = Rsyn::Json::array();
                        for (auto die : expanded.edgeDies) observation["expanded_edge_dies"].push_back(static_cast<int>(die));
                        observation["hbt_nodes"] = expanded.hbtNodes;
                   }
                }
                if (fullTrace) observation["paper_location"] = paperLocationJson(locations);
            } else {
                const int budget = design.hbt->available() - futureReserve;
                if (fullTrace) {
                    observation["hbt_free_sites_before"] = design.hbt->available();
                    observation["hbt_site_budget"] = budget;
                    observation["future_cross_die_reserve"] = futureReserve;
                }
                PhaseTimer timer(phaseSeconds, activeStage);
                locations = HbtLocationOptimizer().runWithSiteBudget(expanded, *design.hbt, *design.routing, budget);
            }
            activeStage = "hbt_occupancy";
            { PhaseTimer timer(phaseSeconds, activeStage); design.hbt->occupy(locations.hbtSites, net.id); }
            if (!paper && design.hbt->available() < futureReserve) throw std::logic_error("HBT allocation violated the future-net reserve");
            remainingCross--;
            if (paper && design.hbt->available() < remainingCross && !report["hbt_resource_audit"].count("first_overcommitment")) {
                report["hbt_resource_audit"]["first_overcommitment"] = {{"net", net.name}, {"completed_cross_die_nets", crossCount},
                    {"used_sites", report["initial_available_hbt_sites"].get<int>() - design.hbt->available()},
                    {"free_sites", design.hbt->available()}, {"remaining_cross_die_nets", remainingCross},
                    {"unavoidable_deficit_without_revisiting_committed_allocations", remainingCross - design.hbt->available()}};
            }
        } else {
            if (paper) locations.mode = "single_die_pattern_input";
            for (const auto& node : expanded.tree.getNodes()) locations.positions.push_back(node.position);
            for (const auto& edge : expanded.tree.getEdges()) locations.wirelength += utils::Dist(locations.positions[edge.u], locations.positions[edge.v]);
        }
        topologyHbtCount += assignment.hbtCount;
        std::set<int> unique(locations.hbtSites.begin(), locations.hbtSites.end());
        if (fullTrace) {
            observation["positions_dbu"] = Rsyn::Json::array();
            for (auto point : locations.positions) observation["positions_dbu"].push_back({point.x, point.y});
            observation["hbt_sites"] = locations.hbtSites;
            observation["optimized_wirelength_dbu"] = locations.wirelength;
            observation["location_mode"] = locations.mode;
            observation["site_budget_fallback"] = locations.budgetFallback;
            observation["remaining_cross_die_nets_after"] = remainingCross;
        }
        report["nets"].push_back({{"id", net.id}, {"name", net.name}, {"cross_die", net.crossesDies()},
            {"topological_hbt_count", assignment.hbtCount}, {"physical_hbt_count", unique.size()},
            {"hbt_sites", std::vector<int>(unique.begin(), unique.end())}, {"optimized_tree_wirelength_dbu", locations.wirelength},
            {"location_candidate_count", locations.candidateCount}, {"location_mode", locations.mode},
            {"hbt_site_budget", locations.siteBudget}, {"site_budget_fallback", locations.budgetFallback},
            {"die_assignment_root", assignment.root}, {"hbt_count_dp_applied", !paper || net.crossesDies()}});
        activeStage = "subnet_decomposition";
        std::vector<Subnet> generated;
        {
            PhaseTimer timer(phaseSeconds, activeStage);
            if (paper) {
                auto partition = partitionAtHbts(expanded, locations);
                if (fullTrace) {
                    observation["paper_decomposition"] = {{"root", partition.root}, {"reverse_dfs_order", partition.reverseDfsOrder},
                                                           {"components", Rsyn::Json::array()}};
                    for (const auto& part : partition.components) observation["paper_decomposition"]["components"].push_back({
                        {"die", static_cast<int>(part.die)}, {"nodes", part.nodes}, {"edges", part.edges}, {"hbt_sites", part.hbtSites}});
                }
                generated = SubnetDecomposer().runPaper(net, expanded, locations, partition, design);
            } else generated = SubnetDecomposer().run(net, expanded, locations, design);
        }
        activeStage = "pattern_routing";
        {
        PhaseTimer timer(phaseSeconds, activeStage);
        for (auto& subnet : generated) {
            auto& router = *design.dies[subnet.die].router;
            subnet.routingIndex = router.getNets().size();
            std::vector<std::vector<GRPoint>> points;
            for (const auto& point : subnet.terminals) points.push_back({point});
            // III-D fixes subnet membership and HBT pins, not the old planar
            // Steiner detours after relocation. III-E uses conventional CUGR
            // pattern routing, which constructs an RSMT on the subnet pins.
            // Retaining the inherited pre-relocation topology can produce long
            // detours even with zero overflow (ethmac author-case regression).
            Net local(subnet.routingIndex, subnet.name);
            for (const auto& pin : subnet.pins) local.addPinRef(pin.instanceIndex, pin.pinIndex);
            GRNet routingNet = subnetAccess == "physical_shapes"
                ? GRNet(local, *design.dies[subnet.die].design, router.getGridGraph())
                : GRNet(subnet.routingIndex, subnet.name, points);
            router.addRoutedNet(std::move(routingNet), subnetTopology == "flute" ? nullptr : subnet.topology);
            subnets.push_back(std::move(subnet));
        }
        }
        report["completed_input_nets"] = report["completed_input_nets"].get<int>() + 1;
        if (fullTrace) {
            observation["status"] = "completed";
            traceFile << observation.dump() << std::endl;
            log() << "H3D net " << net.name << ": " << assignment.hbtCount << " topology HBTs, " << unique.size() << " physical sites" << std::endl;
        }
    }
    // 所有 HBT pad 已成为 Design 的真实固定几何。先同步最终容量并验证 demand 未变，
    // 再让 Pattern/Maze 对包含所有 pad 的资源图修复全部溢出网。
    activeStage = "hbt_pad_capacity_synchronization"; activeNet = "";
    {
        PhaseTimer timer(phaseSeconds, activeStage);
        for (int die = 0; die < 2; die++) {
            auto stats = design.dies[die].router->synchronizeCapacities(*design.dies[die].design);
            report["pad_capacity_sync"][die == 0 ? "bottom" : "top"] = {
                {"hbt_pad_count", design.hbtPorts[die].size()}, {"changed_edges", stats.changedEdges},
                {"capacity_reduction", stats.capacityReduction}, {"demand_preserved", stats.demandPreserved},
                {"matches_fresh_final_geometry", stats.matchesFreshGeometry}};
        }
    }
    for (int die = 0; die < 2; die++) {
        log() << "H3D " << (die == 0 ? "bottom" : "top") << " congestion refinement" << std::endl;
        activeNet = ""; activeStage = die == 0 ? "bottom_congestion_refinement" : "top_congestion_refinement";
        PhaseTimer timer(phaseSeconds, activeStage);
        design.dies[die].router->route(false);
    }
    activeStage = "connectivity_validation";
    { PhaseTimer timer(phaseSeconds, activeStage); validate(design, subnets, selected); }
    } catch (const std::exception& error) {
        observation["status"] = "failed"; observation["stage"] = activeStage; observation["error"] = error.what();
        if (fullTrace) traceFile << observation.dump() << std::endl;
        report["status"] = "failed"; report["failed_net"] = activeNet; report["failed_stage"] = activeStage;
        report["error"] = error.what(); report["connectivity_checked"] = false;
        report["failure_category"] = dynamic_cast<const HbtResourceExhausted*>(&error) ? "hbt_resource_exhaustion" : "routing_or_input_error";
        report["hbt_free_sites_at_failure"] = design.hbt->available();
        report["unprocessed_input_nets"] = selected.size() - report["completed_input_nets"].get<std::size_t>();
        report["allocated_hbt_sites_including_failed_net"] = report["initial_available_hbt_sites"].get<int>() - design.hbt->available();
        report["routing"] = routingSnapshot(design); report["phase_seconds"] = phaseSeconds;
        report["total_seconds"] = std::chrono::duration<double>(Clock::now() - totalStart).count();
        std::ofstream failed((boost::filesystem::path(parameters.output_dir) / "h3d_failure_report.json").string());
        failed << report.dump(2) << '\n';
        throw;
    }
    report["status"] = "completed";
    report["connectivity_checked"] = true;
    report["cross_die_net_count"] = crossCount;
    report["topological_hbt_count"] = topologyHbtCount;
    report["remaining_available_hbt_sites"] = design.hbt->available();
    report["physical_hbt_count"] = report["initial_available_hbt_sites"].get<int>() - design.hbt->available();
    report["hbts"] = Rsyn::Json::array();
    for (const auto& entry : design.hbtPorts[0]) {
        int site = entry.first;
        auto point = design.physicalHbtPoint(site);
        report["hbts"].push_back({{"site", site}, {"owner_net_id", design.hbt->owner(site)},
            {"position_dbu", {point.x, point.y}}, {"bottom_port", design.hbtPortNames[0].at(site)},
            {"top_port", design.hbtPortNames[1].at(site)}});
    }
    report["subnets"] = Rsyn::Json::array();
    for (const auto& subnet : subnets) {
        Rsyn::Json pins = Rsyn::Json::array();
        for (const auto& pin : subnet.pins) {
            auto names = design.dies[subnet.die].design->getPinNames(pin);
            pins.push_back({pin.pinIndex == -1 ? "PIN" : names.first, pin.pinIndex == -1 ? names.first : names.second});
        }
        report["subnets"].push_back({{"name", subnet.name}, {"die", subnet.die == 0 ? "bottom" : "top"},
                                    {"original_net_id", subnet.original}, {"pins", pins}, {"hbt_sites", subnet.hbtSites}});
    }
    report["routing"] = routingSnapshot(design);
    activeStage = "output";
    {
    PhaseTimer timer(phaseSeconds, activeStage);
    for (int die = 0; die < 2; die++) {
        std::string label = die == 0 ? "bottom" : "top";
        auto prefix = (boost::filesystem::path(parameters.output_dir) / label).string();
        design.dies[die].router->write(prefix + ".guide");
        writeDieDef(die == 0 ? parameters.bottom_def_file : parameters.top_def_file, prefix + ".def", design, die, subnets);
    }
    }
    report["phase_seconds"] = phaseSeconds;
    report["total_seconds"] = std::chrono::duration<double>(Clock::now() - totalStart).count();
    std::ofstream output((boost::filesystem::path(parameters.output_dir) / "h3d_report.json").string());
    if (!output) throw std::runtime_error("Cannot write H3D report");
    output << (fullTrace ? report.dump(2) : report.dump()) << std::endl;
    if (!output) throw std::runtime_error("Failed to write H3D report");
    log() << "H3D completed: " << crossCount << " cross-die nets, " << report["physical_hbt_count"] << " legal HBT sites; connectivity checked." << std::endl;
}

} // namespace h3d
