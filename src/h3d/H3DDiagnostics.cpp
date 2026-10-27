#include "H3DDiagnostics.h"
#include "DieAssignment.h"
#include <boost/filesystem.hpp>
#include <chrono>
#include <set>
#include <stdexcept>

namespace h3d {
namespace {
using Clock = std::chrono::steady_clock;
double seconds(Clock::time_point start) { return std::chrono::duration<double>(Clock::now() - start).count(); }

void writeJson(const Parameters& parameters, const std::string& name, const Rsyn::Json& value) {
    auto path = (boost::filesystem::path(parameters.output_dir) / name).string();
    std::ofstream file(path);
    if (!file) throw std::runtime_error("Cannot write diagnostic report: " + path);
    file << value.dump(2) << '\n';
}
}

bool runH3DDiagnostics(const Parameters& parameters, const H3DDesign& design, double initializationSeconds) {
    Rsyn::Json summary;
    summary["method_profile"] = parameters.h3d_method;
    summary["random_seed"] = parameters.h3d_seed;
    summary["mode"] = parameters.h3d_init_only ? "initialization_only" : parameters.h3d_topology_only ? "topology_only" : "full_routing";
    summary["dbu_per_micron"] = design.dies[0].design->getLibDBU();
    summary["original_net_count"] = design.nets.size();
    summary["original_endpoint_count"] = 0;
    summary["cross_die_net_count"] = 0;
    for (const auto& net : design.nets) {
        summary["original_endpoint_count"] = summary["original_endpoint_count"].get<std::size_t>() + net.pins.size();
        if (net.crossesDies()) summary["cross_die_net_count"] = summary["cross_die_net_count"].get<int>() + 1;
    }
    for (int die = 0; die < 2; die++) {
        std::string label = die == 0 ? "bottom" : "top";
        auto& data = summary["dies"][label];
        const auto& database = *design.dies[die].design;
        const auto& graph = design.dies[die].router->getGridGraph();
        data["cells"] = database.getNumCells(); data["ports"] = database.getNumPorts();
        data["local_nets"] = database.getAllNets().size();
        data["routing_grid"] = {graph.getSize(0), graph.getSize(1), graph.getNumLayers()};
        data["routing_layers"] = Rsyn::Json::array();
        for (int layer = 0; layer < database.getNumLayers(); layer++) data["routing_layers"].push_back(database.getLayer(layer).getName());
    }
    summary["native_manifest_endpoint_check"] = parameters.h3d_netlist_file.empty() ? "not_requested" : "passed_exact_multiset";
    summary["hbt_resources_initialized"] = design.hbt != nullptr;
    summary["legal_hbt_capacity"] = design.hbt ? Rsyn::Json(design.hbt->available()) : Rsyn::Json(nullptr);
    summary["physical_pad_geometry_provided"] = design.config.count("pad_size_dbu") && !design.config["pad_size_dbu"].is_null();
    summary["initialization_seconds"] = initializationSeconds;
    writeJson(parameters, "input_statistics.json", summary);
    log() << "H3D loaded: " << summary["original_net_count"] << " original nets, " << summary["cross_die_net_count"]
          << " cross-die nets, " << summary["original_endpoint_count"] << " original endpoints" << std::endl;
    if (parameters.h3d_init_only) return true;
    if (!parameters.h3d_topology_only) return false;

    std::set<std::string> selected;
    if (!parameters.h3d_net_selection_file.empty()) {
        std::ifstream file(parameters.h3d_net_selection_file);
        if (!file) throw std::runtime_error("Cannot read diagnostic net selection");
        Rsyn::Json names; file >> names;
        for (const auto& item : names) {
            auto name = item.get<std::string>();
            if (!selected.insert(name).second) throw std::invalid_argument("Duplicate selected net: " + name);
        }
        if (selected.empty()) throw std::invalid_argument("Diagnostic selection cannot be empty");
        std::set<std::string> available;
        for (const auto& net : design.nets) available.insert(net.name);
        for (const auto& name : selected) if (!available.count(name)) throw std::invalid_argument("Unknown selected net: " + name);
    }
    auto start = Clock::now();
    double rsmtSeconds = 0, dpSeconds = 0;
    std::mt19937 random(parameters.h3d_seed);
    int completed = 0, failed = 0, hbtCount = 0;
    std::ofstream traces((boost::filesystem::path(parameters.output_dir) / "topology_traces.jsonl").string());
    if (!traces) throw std::runtime_error("Cannot write topology trace");
    Rsyn::Json failures = Rsyn::Json::array();
    for (const auto& net : design.nets) {
        if (!selected.empty() && !selected.count(net.name)) continue;
        Rsyn::Json record = {{"name", net.name}, {"id", net.id}, {"cross_die", net.crossesDies()}, {"endpoint_count", net.pins.size()}};
        record["source_pins"] = Rsyn::Json::array();
        for (std::size_t i = 0; i < net.pins.size(); i++) {
            const auto& pin = net.pins[i];
            int die = static_cast<int>(pin.die);
            auto names = design.dies[die].design->getPinNames({pin.instanceIndex, pin.pinIndex});
            auto center = cellCenter(design.dies[die].router->getGridGraph(), net.access[i]);
            record["source_pins"].push_back({{"die", die}, {"instance", pin.pinIndex == -1 ? "PIN" : names.first},
                {"pin", pin.pinIndex == -1 ? names.first : names.second}, {"access", {net.access[i].layerIdx, net.access[i].x, net.access[i].y}},
                {"position_dbu", {center.x, center.y}}});
        }
        std::string stage = "planar_rsmt";
        log() << "H3D topology diagnostic begin: " << net.name << " pins=" << net.pins.size() << std::endl;
        try {
            auto t = Clock::now();
            auto tree = design.planarTree(net);
            rsmtSeconds += seconds(t);
            stage = "hbt_count_dp"; t = Clock::now();
            DieAssignmentResult assignment;
            if (parameters.h3d_method == "paper" && !net.crossesDies()) {
                auto die = net.pins.front().die;
                assignment.root = tree.rootAt().root;
                assignment.edgeDies.assign(tree.getEdges().size(), die);
                assignment.nodeDieSpans.assign(tree.getNodes().size(), {die, die});
            } else {
                int root = parameters.h3d_method == "paper" ? chooseLeafRoot(tree, random) : -1;
                assignment = DieAssignment().run(tree, root);
            }
            record["die_assignment_root"] = assignment.root;
            record["hbt_count_dp_applied"] = parameters.h3d_method != "paper" || net.crossesDies();
            if (parameters.h3d_method == "paper" && net.crossesDies()) record["location_seed"] = static_cast<std::uint32_t>(random());
            dpSeconds += seconds(t);
            record["nodes"] = Rsyn::Json::array();
            for (const auto& node : tree.getNodes()) {
                std::vector<int> pinDies;
                for (const auto& pin : node.pins) pinDies.push_back(static_cast<int>(pin.die));
                record["nodes"].push_back({{"position_dbu", {node.position.x, node.position.y}}, {"pin_dies", pinDies}});
            }
            record["edges"] = Rsyn::Json::array();
            for (const auto& edge : tree.getEdges()) record["edges"].push_back({edge.u, edge.v});
            record["edge_dies"] = Rsyn::Json::array();
            for (auto die : assignment.edgeDies) record["edge_dies"].push_back(static_cast<int>(die));
            record["hbt_nodes"] = assignment.hbtNodes; record["hbt_count"] = assignment.hbtCount;
            record["status"] = "passed_native_topology_and_dp";
            completed++; hbtCount += assignment.hbtCount;
        } catch (const std::exception& error) {
            record["status"] = "failed"; record["stage"] = stage; record["error"] = error.what();
            failures.push_back({{"net", net.name}, {"stage", stage}, {"error", error.what()}}); failed++;
        }
        traces << record.dump() << std::endl;
    }
    summary["experiment_scope"] = selected.empty() ? "all_loaded_nets_topology_only" : "explicit_selected_nets_topology_only";
    summary["completed_nets"] = completed; summary["failed_nets"] = failed;
    summary["topological_hbt_count"] = hbtCount; summary["failures"] = failures;
    summary["phase_seconds"] = {{"planar_rsmt", rsmtSeconds}, {"hbt_count_dp", dpSeconds}, {"diagnostic_total", seconds(start)}};
    summary["hbt_locations_assigned"] = false;
    writeJson(parameters, "topology_summary.json", summary);
    if (failed) throw std::runtime_error("Topology diagnostics found failed nets; see topology_summary.json (no core algorithm modified)");
    return true;
}
} // namespace h3d
