#!/usr/bin/env python3
'''Apply the F2F per-net Pareto integration to the phase-1 source tree.

Run from the repository root after extracting this patch:
    python3 apply_pareto_phase1.py

The script intentionally refuses partial or repeated application.
'''
from pathlib import Path

ROOT = Path(__file__).resolve().parent
ROUTER = ROOT / "src/gr/F2fRouter.cpp"


def replace_once(source: str, old: str, new: str, label: str) -> str:
    count = source.count(old)
    if count != 1:
        raise RuntimeError(
            f"{label}: expected exactly one source match, found {count}. "
            "No further edits were made to this file."
        )
    return source.replace(old, new, 1)


source = ROUTER.read_text(encoding="utf-8")
if '#include "F2fPareto.h"' in source:
    raise SystemExit("F2F Pareto integration already appears to be applied.")

source = replace_once(
    source,
    '#include "F2fCandidateSearch.h"\n#include "GlobalRouter.h"',
    '#include "F2fCandidateSearch.h"\n#include "F2fPareto.h"\n#include "GlobalRouter.h"',
    "include F2fPareto.h",
)

old_policy = '''struct CandidatePolicy {
    bool budgetDp, beam, compareOld, uniqueSteps;
    int maxHbt, maxNodes, beamWidth, maxSites;

    explicit CandidatePolicy(const Json& config) {
        const auto mode = config.value("candidate_mode", std::string("improved"));
        if (mode != "legacy" && mode != "improved") throw std::runtime_error("Invalid candidate_mode");
        const bool improved = mode == "improved";
        const auto opts = config.value("candidate_search", Json::object());
        budgetDp = improved && opts.value("budget_dp", true);
        beam = improved && opts.value("position_beam", true);
        compareOld = improved && opts.value("compare_old", true);
        uniqueSteps = improved && opts.value("deduplicate_steps", true);
        maxHbt = opts.value("max_hbt", 8);
        maxNodes = opts.value("max_projection_nodes", 160);
        beamWidth = opts.value("beam_width", 3);
        maxSites = opts.value("max_sites", 32);
        if (maxHbt < 1 || maxHbt > 16 || maxNodes < 2 || maxNodes > 512 ||
            beamWidth < 1 || beamWidth > 16 || maxSites < 2 || maxSites > 128) {
            throw std::runtime_error("Invalid F2F candidate search bounds");
        }
    }
};'''
new_policy = '''struct CandidatePolicy {
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
};'''
source = replace_once(source, old_policy, new_policy, "CandidatePolicy")

old_job = '''struct Job {
    std::array<std::unique_ptr<GRNet>, 2> sides;
    RoutedNet route;
    int fixedSite = -1;
    Box searchRegion;

    bool crosses() const { return sides[Top] && sides[Bottom]; }
};'''
new_job = '''struct Job {
    std::array<std::unique_ptr<GRNet>, 2> sides;
    RoutedNet route;
    int fixedSite = -1;
    Box searchRegion;
    std::vector<F2fParetoRecord> paretoArchive;

    bool crosses() const { return sides[Top] && sides[Bottom]; }
};'''
source = replace_once(source, old_job, new_job, "Job Pareto archive")

source = replace_once(
    source,
    "for (auto& state : searchF2fDieAssignments(input, graph.policy.maxHbt)) {",
    "for (auto& state : searchF2fDieAssignments(input)) {",
    "dynamic HBT DP call",
)

source = replace_once(
    source,
    '''if (topology.crossings.empty() || (!graph.policy.budgetDp && topology.crossings.size() < 2) ||
            (graph.policy.budgetDp && topology.crossings.size() > graph.policy.maxHbt) ||
            (!compareOverflow && graph.hbtCost * topology.crossings.size() >= best.cost)) continue;''',
    '''if (topology.crossings.empty() || (!graph.policy.budgetDp && topology.crossings.size() < 2) ||
            (!compareOverflow && graph.hbtCost * topology.crossings.size() >= best.cost)) continue;''',
    "remove max_hbt topology filter",
)

old_measure = '''void measureReroute(RoutedNet& route, const F2fGraph& graph) {
    route.wireOverflow = 0;
    route.resourceOverflow = 0;
    for (int d = Top; d <= Bottom; ++d) {
        const auto metrics = graph.dies[d].graph().measureForest(route.components[d], true, graph.policy.uniqueSteps);
        route.wireOverflow += metrics.wireOverflowDelta;
        route.resourceOverflow += metrics.resourceOverflowDelta;
    }
}'''
new_measure = old_measure + '''

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
}'''
source = replace_once(source, old_measure, new_measure, "Pareto metric measurement helper")

source = replace_once(
    source,
    '''RoutedNet planJob(F2fGraph& graph, const Job& job, int id, const Parameters& params, Stage stage,
                  const std::array<GridGraphView<bool>, 2>& congestion) {
    RoutedNet best;
    int single = 0, multi = 0, unavailable = 0, incomplete = 0, expansions = 0, collisions = 0, retained = 0;''',
    '''RoutedNet planJob(F2fGraph& graph, Job& job, int id, const Parameters& params, Stage stage,
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

    int single = 0, multi = 0, unavailable = 0, incomplete = 0, expansions = 0, collisions = 0, retained = 0;''',
    "planJob mutable Job and incumbent",
)

old_evaluate = '''    auto evaluate = [&](const Topology& topology, const vector<int>& sites) {
        RoutedNet plan;
        if (!check || validTopology(topology, job, sites, graph, id)) {
            plan = evaluateTopology(topology, sites, job, graph, params, stage, congestion);
        }
        if (compareOverflow && std::isfinite(plan.cost)) measureReroute(plan, graph);
        const bool wouldImprove = improves(plan, best, 0.0);
        if (wouldImprove && check) {
            for (int d = Top; d <= Bottom; ++d) {
                for (const auto& component : plan.components[d]) {
                    if (!graph.dies[d].graph().connectsPins(component)) {
                        plan.cost = std::numeric_limits<CostT>::infinity();
                    }
                }
            }
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
    };'''
new_evaluate = '''    auto evaluate = [&](const Topology& topology, const vector<int>& sites) {
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
    };'''
source = replace_once(source, old_evaluate, new_evaluate, "candidate evaluation and Pareto insertion")

old_finish = '''    auto finish = [&] {
        if (graph.diagnostics.enabled()) {
            graph.diagnostics.write({{"type", "search"}, {"net", id}, {"stage", phase(stage)},
                {"single_evaluated", single}, {"multi_evaluated", multi}, {"empty_site_lists", unavailable},
                {"incomplete_combinations", incomplete}, {"expansions", expansions},
                {"retained_dp_topologies", retained}, {"beam_collision_rejections", collisions},
                {"feasible", std::isfinite(best.cost)}});
        }
        return std::move(best);
    };'''
new_finish = '''    auto finish = [&] {
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
    };'''
source = replace_once(source, old_finish, new_finish, "Pareto archive diagnostics")

source = replace_once(
    source,
    "void writeOutputs(F2fGraph& graph, const vector<Job>& jobs, const std::string& hbtFile) {",
    "void writeOutputs(F2fGraph& graph, const vector<Job>& jobs, const std::string& hbtFile,\n                  const std::string& paretoFile) {",
    "writeOutputs signature",
)

old_paths = '''    if (graph.dies[Top].guideFile == graph.dies[Bottom].guideFile ||
        graph.dies[Top].guideFile == hbtFile || graph.dies[Bottom].guideFile == hbtFile) {
        throw std::runtime_error("F2F outputs must have distinct paths");
    }'''
new_paths = '''    if (graph.dies[Top].guideFile == graph.dies[Bottom].guideFile ||
        graph.dies[Top].guideFile == hbtFile || graph.dies[Bottom].guideFile == hbtFile ||
        (!paretoFile.empty() && (paretoFile == graph.dies[Top].guideFile ||
                                 paretoFile == graph.dies[Bottom].guideFile ||
                                 paretoFile == hbtFile))) {
        throw std::runtime_error("F2F outputs must have distinct paths");
    }'''
source = replace_once(source, old_paths, new_paths, "output path collision check")

old_open = '''    std::ofstream bonds(hbtFile);
    if (!guides[Top] || !guides[Bottom] || !bonds) throw std::runtime_error("Cannot open F2F outputs");
    bonds << "net_id\\ttop_net\\tbottom_net\\tsite_id\\tpackage_x\\tpackage_y\\tpackage_dbu";'''
new_open = '''    std::ofstream bonds(hbtFile);
    std::ofstream pareto;
    if (!paretoFile.empty()) {
        pareto.open(paretoFile);
        if (!pareto) throw std::runtime_error("Cannot open F2F Pareto output: " + paretoFile);
        pareto << "net_id\\ttop_net\\tbottom_net\\tstage\\trank\\thbt_count\\twirelength_um\\tvia_count"
                  "\\twire_overflow_delta\\tresource_overflow_delta\\tcongestion_cost\\tsite_ids\\n";
    }
    if (!guides[Top] || !guides[Bottom] || !bonds) throw std::runtime_error("Cannot open F2F outputs");
    bonds << "net_id\\ttop_net\\tbottom_net\\tsite_id\\tpackage_x\\tpackage_y\\tpackage_dbu";'''
source = replace_once(source, old_open, new_open, "open Pareto TSV")

old_hbt_loop_end = '''            bonds << '\\t' << hbt.fixed << '\\n';
        }
    }
    for (auto& guide : guides) guide.close();
    bonds.close();
    if (!guides[Top] || !guides[Bottom] || !bonds) throw std::runtime_error("Cannot write F2F outputs");'''
new_hbt_loop_end = '''            bonds << '\\t' << hbt.fixed << '\\n';
        }
        if (pareto) {
            const auto topName = job.sides[Top] ? job.sides[Top]->getName() : std::string();
            const auto bottomName = job.sides[Bottom] ? job.sides[Bottom]->getName() : std::string();
            for (std::size_t rank = 0; rank < job.paretoArchive.size(); ++rank) {
                const auto& candidate = job.paretoArchive[rank];
                const auto& m = candidate.metrics;
                pareto << i << '\\t' << topName << '\\t' << bottomName << '\\t'
                       << candidate.stage << '\\t' << rank << '\\t' << m.hbtCount << '\\t'
                       << m.wireLengthUm << '\\t' << m.viaCount << '\\t'
                       << m.wireOverflowDelta << '\\t' << m.resourceOverflowDelta << '\\t'
                       << m.congestionCost << '\\t';
                for (std::size_t site = 0; site < candidate.sites.size(); ++site) {
                    if (site) pareto << ',';
                    pareto << candidate.sites[site];
                }
                pareto << '\\n';
            }
        }
    }
    for (auto& guide : guides) guide.close();
    bonds.close();
    if (pareto) pareto.close();
    if (!guides[Top] || !guides[Bottom] || !bonds || (!paretoFile.empty() && !pareto)) {
        throw std::runtime_error("Cannot write F2F outputs");
    }'''
source = replace_once(source, old_hbt_loop_end, new_hbt_loop_end, "write Pareto TSV rows")

old_run = '''    F2fGraph graph(parameters, config);
    auto jobs = buildJobs(graph, config);'''
new_run = '''    const auto paretoFile = config.value("pareto_output", std::string());
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
    auto jobs = buildJobs(graph, config);'''
source = replace_once(source, old_run, new_run, "Pareto output path validation")

source = replace_once(
    source,
    'writeOutputs(graph, jobs, config.at("hbt_output").get<std::string>());',
    'writeOutputs(graph, jobs, config.at("hbt_output").get<std::string>(), paretoFile);',
    "writeOutputs call",
)

# Keep the integration test aligned with the removal of the max_hbt knob.
TEST = ROOT / "tests/h3d/test_candidate_phase1.py"
test_source = TEST.read_text(encoding="utf-8")
test_source = replace_once(
    test_source,
    'search={"max_sites": 2, "max_hbt": 1})',
    'search={"max_sites": 2})',
    "remove obsolete max_hbt test setting",
)

# Write only after all expected anchors in both files have been validated.
ROUTER.write_text(source, encoding="utf-8")
TEST.write_text(test_source, encoding="utf-8")
print("Patched src/gr/F2fRouter.cpp successfully.")
print("Updated tests/h3d/test_candidate_phase1.py successfully.")
print("F2fCandidateSearch and F2fPareto source files are supplied separately.")
