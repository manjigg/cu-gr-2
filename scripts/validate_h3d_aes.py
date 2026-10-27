#!/usr/bin/env python3
"""Independent AES checks: enumerate die assignments, score physical sites and rebuild connectivity."""
import argparse
from collections import Counter, defaultdict, deque
import hashlib
import itertools
import json
from pathlib import Path
import re
import time

from h3d_aes_experiment import ROOT, def_data, section, experiment_source


def traces(path):
    with path.open() as file:
        for line in file:
            yield json.loads(line)


def assignment_cost(nodes, edges, labels):
    masks = [sum(1 << die for die in set(node["pin_dies"])) for node in nodes]
    for (u, v), die in zip(edges, labels):
        masks[u] |= 1 << die
        masks[v] |= 1 << die
    return sum(mask == 3 for mask in masks)


def independent_mask_dp(nodes, edges):
    """Accumulate the union of incident die masks, rather than enumerate die intervals."""
    adjacent = [[] for _ in nodes]
    for u, v in edges:
        adjacent[u].append(v)
        adjacent[v].append(u)
    parents = [-2] * len(nodes)
    parents[0] = -1
    order = [0]
    for u in order:
        for v in adjacent[u]:
            if v == parents[u]:
                continue
            assert parents[v] == -2, "Cycle in planar topology"
            parents[v] = u
            order.append(v)
    assert len(order) == len(nodes) and len(edges) == len(nodes) - 1
    costs = [None] * len(nodes)
    for u in reversed(order):
        initial = sum(1 << d for d in set(nodes[u]["pin_dies"]))
        states = {}
        for parent in (-1, 0, 1):
            masks = {initial | (0 if parent == -1 else 1 << parent): 0}
            for v in adjacent[u]:
                if v == parents[u]:
                    continue
                next_masks = {}
                for mask, value in masks.items():
                    for die in (0, 1):
                        new_mask = mask | (1 << die)
                        candidate = value + costs[v][die]
                        next_masks[new_mask] = min(next_masks.get(new_mask, float("inf")), candidate)
                masks = next_masks
            states[parent] = min(value + (mask == 3) for mask, value in masks.items())
        costs[u] = states
    return costs[0][-1]


def verify_topology(experiment):
    source_root = experiment_source(experiment)
    start = time.monotonic()
    source_endpoints = defaultdict(Counter)
    for die in ("top", "bottom"):
        for name, pins in def_data(source_root / (die + ".def"))["nets"].items():
            source_endpoints[name].update((die, instance, pin) for instance, pin in pins)
    small = list(traces(experiment / "small_topology/topology_traces.jsonl"))
    examples = []
    for record in small:
        assert record["status"] == "passed_native_topology_and_dp"
        nodes, edges = record["nodes"], record["edges"]
        assert len(edges) <= 18, "Selected tree too large for exhaustive oracle"
        exact = min(assignment_cost(nodes, edges, labels) for labels in itertools.product((0, 1), repeat=len(edges)))
        assert exact == record["hbt_count"]
        examples.append({"net": record["name"], "endpoints": record["endpoint_count"], "edges": len(edges),
                         "enumerated_assignments": 2 ** len(edges), "exact_minimum": exact})
    count, hbt_sum, max_nodes, endpoints = 0, 0, 0, 0
    checked_names = set()
    for record in traces(experiment / "all_topology/topology_traces.jsonl"):
        assert record["status"] == "passed_native_topology_and_dp"
        nodes, edges = record["nodes"], record["edges"]
        assert record["name"] not in checked_names
        checked_names.add(record["name"])
        loaded = Counter(("bottom" if pin["die"] == 0 else "top", pin["instance"], pin["pin"])
                         for pin in record["source_pins"])
        assert loaded == source_endpoints[record["name"]], "Loaded endpoint multiset differs from raw DEF for " + record["name"]
        expected_pins = Counter((tuple(pin["position_dbu"]), pin["die"]) for pin in record["source_pins"])
        actual_pins = Counter((tuple(node["position_dbu"]), die) for node in nodes for die in node["pin_dies"])
        assert expected_pins == actual_pins, "Projection lost an original endpoint"
        exact = independent_mask_dp(nodes, edges)
        assert exact == record["hbt_count"] == assignment_cost(nodes, edges, record["edge_dies"])
        count += 1
        endpoints += record["endpoint_count"]
        hbt_sum += exact
        max_nodes = max(max_nodes, len(nodes))
    assert checked_names == set(source_endpoints)
    result = {"status": "passed", "selected_full_enumeration": examples,
              "independent_incident_mask_dp_nets": count, "original_endpoints_checked": endpoints,
              "topological_hbt_count": hbt_sum, "maximum_planar_nodes": max_nodes,
              "all_loaded_net_endpoint_multisets_match_raw_def": True,
              "wall_seconds": time.monotonic() - start,
              "scope": "Exact die assignment on the native fixed FLUTE topologies; not HBT placement or DRC"}
    (experiment / "independent_topology_verification.json").write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps(result, indent=2))


def l1(a, b):
    return abs(a[0] - b[0]) + abs(a[1] - b[1])


class Connectivity:
    def __init__(self):
        self.parents = {}

    def root(self, node):
        self.parents.setdefault(node, node)
        path = []
        while self.parents[node] != node:
            path.append(node)
            node = self.parents[node]
        for item in path:
            self.parents[item] = node
        return node

    def connect(self, a, b):
        self.parents[self.root(a)] = self.root(b)


def verify_routing(experiment, stage_name, output_path=None):
    source_root = experiment_source(experiment)
    start = time.monotonic()
    folder = experiment / stage_name
    success_path = folder / "h3d_report.json"
    failed_path = folder / "h3d_failure_report.json"
    report = json.loads((success_path if success_path.is_file() else failed_path).read_text())
    initial = json.loads((folder / "hbt_grid_initial.json").read_text())["sites"]
    positions = {site["id"]: site["position_dbu"] for site in initial}
    owners = {site["id"]: site["initial_owner"] for site in initial}
    claims = {}
    exact_locations = []
    observed = []
    remaining_cross = report.get("selected_cross_die_net_count", sum(item["cross_die"] for item in report["nets"]))
    budget_checks = 0
    for record in traces(folder / "routing_traces.jsonl"):
        if record["status"] != "completed":
            continue
        observed.append(record["id"])
        planar = record["planar"]
        assert assignment_cost(planar["nodes"], planar["edges"], record["edge_dies"]) == record["hbt_count"]
        assert independent_mask_dp(planar["nodes"], planar["edges"]) == record["hbt_count"]
        if "hbt_site_budget" in record:
            remaining_cross -= 1
            assert record["future_cross_die_reserve"] == remaining_cross
            assert record["hbt_free_sites_before"] == sum(owner == -1 for owner in owners.values())
            assert record["hbt_site_budget"] == record["hbt_free_sites_before"] - remaining_cross
            budget_checks += 1
        expanded = record["expanded"]
        selected_positions = record["positions_dbu"]
        for index, node in enumerate(expanded["nodes"]):
            if node["pin_dies"]:
                assert selected_positions[index] == node["position_dbu"], "Location optimization moved a fixed pin"
        score = sum(l1(selected_positions[u], selected_positions[v]) for u, v in expanded["edges"])
        assert score == record["optimized_wirelength_dbu"]
        for node, site in zip(record["hbt_nodes"], record["hbt_sites"]):
            assert owners[site] == -1 and selected_positions[node] == positions[site], "HBT conflict/illegal position"
        movable = [i for i, node in enumerate(expanded["nodes"]) if not node["pin_dies"]]
        if len(movable) == 1 and record["hbt_nodes"] == movable:
            node = movable[0]
            def cost(point):
                return sum(l1(point if u == node else selected_positions[u],
                              point if v == node else selected_positions[v]) for u, v in expanded["edges"])
            exact = min(cost(positions[site]) for site in positions if owners[site] == -1)
            exact_locations.append({"net": record["name"], "native_wirelength_dbu": score,
                                    "exhaustive_all_free_sites_dbu": exact, "gap_dbu": score - exact})
        for site in set(record["hbt_sites"]):
            owners[site] = record["id"]
            claims[site] = record["id"]
        if "hbt_site_budget" in record:
            assert len(set(record["hbt_sites"])) <= record["hbt_site_budget"], "Physical HBT budget was exceeded"
            assert sum(owner == -1 for owner in owners.values()) >= record["future_cross_die_reserve"], "Future-net HBT reserve was consumed"
    result = {"native_status": report.get("status"), "completed_trace_nets": len(observed),
              "independently_checked_site_claims": len(claims), "conflicts": 0,
              "independent_count_dp_nets": len(observed), "future_capacity_budget_checks": budget_checks,
              "full_grid_location_enumerations": exact_locations}
    if report.get("status") != "completed":
        result.update(scope="completed prefix of a failed full run; no full-design connectivity claim",
                      failed_net=report.get("failed_net"), failed_stage=report.get("failed_stage"))
    else:
        if budget_checks:
            assert remaining_cross == 0
        if "pad_capacity_sync" in report:
            for item in report["pad_capacity_sync"].values():
                assert item["demand_preserved"] and item["matches_fresh_final_geometry"]
                assert item["capacity_reduction"] >= -1e-9
        output, endpoint_net, all_original = {}, {}, {}
        source_groups = defaultdict(list)
        connectivity = Connectivity()
        for die in ("top", "bottom"):
            source = def_data(source_root / (die + ".def"))
            output[die] = def_data(folder / (die + ".def"))
            assert source["components"] == output[die]["components"], "Placement changed"
            for name, body in source["ports"].items():
                before = re.sub(r"\+\s*NET\s+\S+", "", body).split()
                after = re.sub(r"\+\s*NET\s+\S+", "", output[die]["ports"][name]).split()
                assert before == after, "Original I/O geometry, placement or direction changed: " + name
            for name, endpoints in source["nets"].items():
                for instance, pin in endpoints:
                    key = (die, instance, pin)
                    all_original[key] = name
                    source_groups[name].append(key)
            for name, endpoints in output[die]["nets"].items():
                for instance, pin in endpoints:
                    key = (die, instance, pin)
                    assert key not in endpoint_net, "Output endpoint duplicated"
                    endpoint_net[key] = (die, name)
            expected_guides = {item["name"] for item in report["subnets"] if item["die"] == die}
            guide = (folder / (die + ".guide")).read_text().splitlines()
            guide_names, rectangle_count = set(), 0
            index = 0
            while index < len(guide):
                name = guide[index].strip()
                assert name not in guide_names and guide[index + 1].strip() == "("
                guide_names.add(name)
                index += 2
                count = 0
                while guide[index].strip() != ")":
                    row = guide[index].split()
                    x1, y1, x2, y2 = map(int, row[:4])
                    assert 0 <= x1 < x2 <= 250000 and 0 <= y1 < y2 <= 250000
                    assert row[4] in {"metal" + str(i) for i in range(1, 7)}
                    index += 1
                    count += 1
                assert count
                rectangle_count += count
                index += 1
            assert guide_names == expected_guides
            result[die + "_guide_rectangles"] = rectangle_count
        assert all(key in endpoint_net for key in all_original), "Output dropped original endpoints"
        hbt_keys = set()
        for item in report["hbts"]:
            assert claims[item["site"]] == item["owner_net_id"]
            connected = []
            for die in ("top", "bottom"):
                name = item[die + "_port"]
                key = (die, "PIN", name)
                hbt_keys.add(key)
                body = output[die]["ports"][name]
                point = re.search(r"\+\s*FIXED\s*\(\s*(-?\d+)\s+(-?\d+)\s*\)", body)
                assert point and list(map(int, point.groups())) == item["position_dbu"] == positions[item["site"]]
                assert re.search(r"\+\s*LAYER\s+metal6\b", body)
                rectangle = re.search(r"\+\s*LAYER\s+metal6\s*\(\s*(-?\d+)\s+(-?\d+)\s*\)\s*\(\s*(-?\d+)\s+(-?\d+)\s*\)", body)
                pad = report["physical_configuration"]["pad_size_dbu"]
                assert rectangle and list(map(int, rectangle.groups())) == [-(pad[0] // 2), -(pad[1] // 2),
                                                                            pad[0] - pad[0] // 2, pad[1] - pad[1] // 2]
                connected.append(endpoint_net[key])
            connectivity.connect(*connected)
        assert set(endpoint_net) == set(all_original) | hbt_keys
        processed_names = {item["name"] for item in report["nets"]}
        component_owner = {}
        for name in processed_names:
            roots = {connectivity.root(endpoint_net[key]) for key in source_groups[name]}
            assert len(roots) == 1, "Original 3D net is disconnected: " + name
            root = roots.pop()
            assert root not in component_owner, "Original 3D nets shorted together"
            component_owner[root] = name
        for key, name in all_original.items():
            if name not in processed_names:
                assert endpoint_net[key] == (key[0], name), "Unprocessed net was altered"
        result.update(status="passed_output_connectivity_and_preservation", processed_nets_checked=len(processed_names),
                      original_endpoints_preserved=len(all_original), unprocessed_original_nets_preserved=len(source_groups) - len(processed_names),
                      original_io_geometry_preserved=True,
                      hbt_pad_dimensions_checked_dbu=report["physical_configuration"]["pad_size_dbu"],
                      guide_not_detailed_routing=True, physical_drc_evaluated=False)
    result["wall_seconds"] = time.monotonic() - start
    (output_path or folder / "independent_verification.json").write_text(json.dumps(result, indent=2) + "\n")
    display = dict(result)
    if len(exact_locations) > 20:
        display["full_grid_location_enumerations"] = {"checked_nets": len(exact_locations),
            "nonzero_gap_count": sum(item["gap_dbu"] != 0 for item in exact_locations),
            "maximum_gap_dbu": max(item["gap_dbu"] for item in exact_locations)}
    print(json.dumps(display, indent=2))


def replay_old_locations(experiment):
    baseline = ROOT / "eval/h3d_aes/run_001"
    verified = json.loads((baseline / "full/independent_verification.json").read_text())
    gaps = {item["net"]: item for item in verified["full_grid_location_enumerations"] if item["gap_dbu"] > 0}
    grid = json.loads((baseline / "full/hbt_grid_initial.json").read_text())["sites"]
    xs = sorted({item["position_dbu"][0] for item in grid})
    ys = sorted({item["position_dbu"][1] for item in grid})
    owners = {item["id"]: item["initial_owner"] for item in grid}
    axis = [(low + min(low + 4200, 250000)) // 2 for low in range(0, 250000, 4200)]
    cases = []
    for record in traces(baseline / "full/routing_traces.jsonl"):
        if record["status"] != "completed":
            continue
        if record["name"] in gaps:
            cases.append({"name": record["name"], "hbt_x": xs, "hbt_y": ys, "routing_x": axis, "routing_y": axis,
                          "available_sites": [site for site, owner in owners.items() if owner == -1],
                          "nodes": record["expanded"]["nodes"], "edges": record["expanded"]["edges"],
                          "hbt_nodes": record["hbt_nodes"], "old_cost_dbu": record["optimized_wirelength_dbu"],
                          "expected_cost_dbu": gaps[record["name"]]["exhaustive_all_free_sites_dbu"]})
        for site in set(record["hbt_sites"]):
            owners[site] = record["id"]
    folder = experiment / "historical_location_replay"
    folder.mkdir(exist_ok=False)
    source = folder / "cases.json"
    source.write_text(json.dumps({"cases": cases}, indent=2) + "\n")
    import subprocess
    command = [str(ROOT / "build/h3d_location_replay"), str(source), str(folder / "result.json")]
    with (folder / "run.log").open("w") as log:
        result = subprocess.run(command, cwd=ROOT, stdout=log, stderr=subprocess.STDOUT, timeout=120)
    (folder / "command_and_status.json").write_text(json.dumps({"argv": command, "exit_code": result.returncode}, indent=2) + "\n")
    assert result.returncode == 0
    report = json.loads((folder / "result.json").read_text())
    assert report["case_count"] == len(gaps) == 20
    print("Replayed all 20 original availability snapshots: repaired costs equal the all-site oracle.")


def pruning_evidence(experiment):
    folder = experiment / "evidence"
    folder.mkdir(exist_ok=True)
    verified = json.loads((experiment / "full/independent_verification.json").read_text())
    gaps = {item["net"]: item for item in verified["full_grid_location_enumerations"] if item["gap_dbu"] > 0}
    initial = json.loads((experiment / "full/hbt_grid_initial.json").read_text())["sites"]
    positions = {site["id"]: site["position_dbu"] for site in initial}
    owners = {site["id"]: site["initial_owner"] for site in initial}
    evidence = []
    for record in traces(experiment / "full/routing_traces.jsonl"):
        if record["status"] != "completed":
            continue
        if record["name"] in gaps:
            node = record["hbt_nodes"][0]
            edges = record["expanded"]["edges"]
            points = record["positions_dbu"]
            neighbors = [points[v if u == node else u] for u, v in edges if u == node or v == node]
            axes = [sorted(p[axis] for p in neighbors) for axis in (0, 1)]
            low = [axis[(len(axis) - 1) // 2] for axis in axes]
            high = [axis[len(axis) // 2] for axis in axes]
            candidates, nearest = set(), {}
            free = {site: p for site, p in positions.items() if owners[site] == -1}
            for site, point in free.items():
                dx = -1 if point[0] < low[0] else 1 if point[0] > high[0] else 0
                dy = -1 if point[1] < low[1] else 1 if point[1] > high[1] else 0
                if dx == dy == 0:
                    candidates.add(site)
                else:
                    distance = sum(max(0, point[a] - high[a], low[a] - point[a]) for a in (0, 1))
                    nearest[(dx, dy)] = min(nearest.get((dx, dy), (float("inf"), -1)), (distance, site))
            candidates.update(site for _, site in nearest.values())
            def score(site):
                return sum(l1(positions[site] if u == node else points[u],
                              positions[site] if v == node else points[v]) for u, v in edges)
            candidate_best = min(map(score, candidates))
            global_site = min(free, key=lambda site: (score(site), site))
            assert candidate_best == record["optimized_wirelength_dbu"], "Observed gap is not explained by candidate pruning"
            item = {**gaps[record["name"]], "candidate_set_optimum_dbu": candidate_best,
                    "native_matches_restricted_optimum": True, "free_sites": len(free),
                    "neighbor_count": len(neighbors), "best_global_site": global_site,
                    "native_site": record["hbt_sites"][0]}
            evidence.append(item)
            if len(evidence) == 1:
                actual = {**item, "fixed_neighbors_dbu": neighbors, "continuous_region": {"low": low, "high": high},
                          "native_candidate_sites": sorted(candidates), "best_global_position_dbu": positions[global_site],
                          "all_free_sites": [{"id": site, "position_dbu": p} for site, p in free.items()],
                          "expanded_tree": record["expanded"], "hbt_node": node}
                (folder / "actual_location_gap.json").write_text(json.dumps(actual, indent=2) + "\n")
        for site in set(record["hbt_sites"]):
            owners[site] = record["id"]
    result = {"single_movable_hbt_cases_enumerated": len(verified["full_grid_location_enumerations"]),
              "global_optimum_gaps": evidence, "all_gaps_explained_by_candidate_pruning": len(evidence) == len(gaps),
              "core_algorithm_modified": False,
              "interpretation": "Formula-7 DP matches the pruned candidate optimum; eight-direction candidate reduction does not guarantee the all-free-site optimum."}
    (folder / "candidate_pruning_summary.json").write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps({"gap_cases": len(evidence), "first_case": evidence[0] if evidence else None,
                      "all_explained_by_pruning": result["all_gaps_explained_by_candidate_pruning"]}, indent=2))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--experiment", type=Path, required=True)
    parser.add_argument("--phase", choices=["topology", "routing", "pruning-evidence", "replay-old-locations"], required=True)
    parser.add_argument("--stage", default="small_routing")
    parser.add_argument("--verification-output", type=Path, help="Write verification into a new experiment while preserving saved input artifacts")
    args = parser.parse_args()
    if args.phase == "topology":
        verify_topology(args.experiment.resolve())
    elif args.phase == "routing":
        verify_routing(args.experiment.resolve(), args.stage, args.verification_output)
    elif args.phase == "pruning-evidence":
        pruning_evidence(args.experiment.resolve())
    else:
        replay_old_locations(args.experiment.resolve())


if __name__ == "__main__":
    main()
