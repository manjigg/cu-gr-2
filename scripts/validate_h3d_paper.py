#!/usr/bin/env python3
"""Independent checks of III-B, Eq6, Algorithm 1/Eq7, and reverse-DFS HBT cuts."""
import argparse
from collections import defaultdict, deque
import json
import itertools
from pathlib import Path
import time

from h3d_aes_experiment import def_data, digest, experiment_source
from validate_h3d_aes import traces, l1, independent_mask_dp, assignment_cost


def candidate_union(points, box):
    lx, ly, hx, hy = box
    inside, nearest, distances = set(), defaultdict(list), {}
    for site, (x, y) in points.items():
        dx = -1 if x < lx else 1 if x > hx else 0
        dy = -1 if y < ly else 1 if y > hy else 0
        if dx == dy == 0:
            inside.add(site)
        else:
            distance = max(lx - x, 0, x - hx) + max(ly - y, 0, y - hy)
            direction = (dx, dy)
            if direction not in distances or distance < distances[direction]:
                distances[direction] = distance
                nearest[direction] = [site]
            elif distance == distances[direction]:
                nearest[direction].append(site)
    return sorted(inside | {site for values in nearest.values() for site in values})


def point_box_distance(point, box):
    return max(box[0] - point[0], 0, point[0] - box[2]) + max(box[1] - point[1], 0, point[1] - box[3])


def midpoint(box):
    def half(value):
        return value // 2 if value >= 0 else -((-value) // 2)
    return [half(box[0] + box[2]), half(box[1] + box[3])]


def verify_location(record, sites, owners, routing):
    tree = record["expanded"]
    nodes, edges = tree["nodes"], tree["edges"]
    details = record["paper_location"]
    assert record["location_mode"] == "paper_region_eight_directions_dp"
    assert details["directional_ties"] == "all_nearest" and not record["site_budget_fallback"]
    assert "hbt_site_budget" not in record and "future_cross_die_reserve" not in record
    free = {i for i, node in enumerate(nodes) if not node["pin_dies"]}
    hbts = set(record["hbt_nodes"])
    adjacency = [[] for _ in nodes]
    for u, v in edges:
        adjacency[u].append(v)
        adjacency[v].append(u)
    visited, expected_order, children = set(), [], {}
    for root in details["roots"]:
        assert root in free and root not in visited
        visited.add(root)
        queue = deque([root])
        while queue:
            node = queue.popleft()
            expected_order.append(node)
            children[node] = []
            for child in adjacency[node]:
                if child not in free:
                    children[node].append(child)
                elif child not in visited:
                    visited.add(child)
                    children[node].append(child)
                    queue.append(child)
    assert visited == free and expected_order == details["bfs_order"]
    assert [item["node"] for item in details["nodes"]] == expected_order
    audits = {item["node"]: item for item in details["nodes"]}
    regions, points, costs, continuous_min, first_choices = {}, {}, {}, {}, {}
    for i, node in enumerate(nodes):
        if node["pin_dies"]:
            assert record["positions_dbu"][i] == node["position_dbu"], "Fixed pin moved"
            x, y = node["position_dbu"]
            regions[i], points[i], costs[i], continuous_min[i] = [x, y, x, y], [[x, y]], [0], 0
    hbt_points = {site: point for site, point in sites.items() if owners[site] == -1}
    routing_bounds = [min(p[0] for p in routing.values()), min(p[1] for p in routing.values()),
                      max(p[0] for p in routing.values()), max(p[1] for p in routing.values())]
    candidate_states, unfixed_leaves = 0, 0
    for node in reversed(expected_order):
        audit = audits[node]
        assert audit["children"] == children[node]
        assert audit["domain"] == ("hbt" if node in hbts else "routing")
        if not children[node]:
            region = routing_bounds[:]
            unfixed_leaves += 1
        else:
            region = [0] * 4
            for axis in (0, 1):
                ends = sorted(value for child in children[node] for value in (regions[child][axis], regions[child][axis + 2]))
                k = len(children[node])
                region[axis], region[axis + 2] = ends[k - 1], ends[k]
        assert region == audit["region"], "Equation-6 median region differs"
        regions[node] = region
        continuous_min[node] = sum(continuous_min[child] + point_box_distance(midpoint(region), regions[child]) for child in children[node])
        domain = hbt_points if node in hbts else routing
        candidate_ids = candidate_union(domain, region)
        assert candidate_ids == audit["candidate_sites"], "Algorithm-1 candidate union differs"
        assert candidate_ids
        points[node] = [domain[site] for site in candidate_ids]
        candidate_states += len(candidate_ids)
        expected_costs, expected_choices = [], []
        for point in points[node]:
            total, choices = 0, []
            for child in children[node]:
                values = [value + l1(point, target) for value, target in zip(costs[child], points[child])]
                best = min(range(len(values)), key=lambda index: (values[index], index))
                choices.append(best)
                total += values[best]
            expected_costs.append(total)
            expected_choices.append(choices)
        assert expected_costs == audit["costs"], "Equation-7 costs differ"
        assert len(audit["child_choices"]) == len(points[node])
        for i, point in enumerate(points[node]):
            assert len(audit["child_choices"][i]) == len(children[node])
            for slot, child in enumerate(children[node]):
                choice = audit["child_choices"][i][slot]
                assert 0 <= choice < len(points[child])
                assert costs[child][choice] + l1(point, points[child][choice]) == costs[child][expected_choices[i][slot]] + l1(point, points[child][expected_choices[i][slot]]), "Equation-7 predecessor is not a minimizing state"
        first_choices[node] = expected_choices
        costs[node] = expected_costs
    canonical = {root: min(range(len(costs[root])), key=lambda i: (costs[root][i], i)) for root in details["roots"]}
    for node in expected_order:
        for slot, child in enumerate(children[node]):
            if child in free:
                canonical[child] = first_choices[node][canonical[node]][slot]
    original_sites = {audits[node]["candidate_sites"][canonical[node]] for node in hbts}
    policy = details.get("traceback_tie_policy", "first")
    assert policy in ("first", "coalesce")
    selected = {root: audits[root]["selected"] for root in details["roots"]}
    for root in details["roots"]:
        assert 0 <= selected[root] < len(costs[root]) and costs[root][selected[root]] == min(costs[root])
    continuous = [node["position_dbu"][:] for node in nodes]
    for root in details["roots"]:
        continuous[root] = midpoint(regions[root])
    for node in expected_order:
        audit = audits[node]
        assert audit["selected"] == selected[node]
        assert record["positions_dbu"][node] == points[node][selected[node]]
        for slot, child in enumerate(children[node]):
            if child in free:
                selected[child] = audit["child_choices"][selected[node]][slot]
                continuous[child] = [max(regions[child][a], min(regions[child][a + 2], continuous[node][a])) for a in (0, 1)]
    fixed_edges = sum(l1(nodes[u]["position_dbu"], nodes[v]["position_dbu"]) for u, v in edges if u not in free and v not in free)
    score = sum(l1(record["positions_dbu"][u], record["positions_dbu"][v]) for u, v in edges)
    assert score == record["optimized_wirelength_dbu"] == fixed_edges + sum(min(costs[root]) for root in details["roots"])
    if "sites_before_tie_break" in details:
        assert details["sites_before_tie_break"] == len(original_sites)
        assert details["wirelength_before_tie_break_dbu"] == score
        common = sorted(set.intersection(*(set(audits[node]["candidate_sites"]) for node in hbts)))
        trials, best_common = 0, None
        if policy == "coalesce" and len(original_sites) > 1:
            for site in common:
                trials += 1
                restricted = {i: [0] for i in range(len(nodes)) if i not in free}
                for node in reversed(expected_order):
                    restricted[node] = [float("inf") if node in hbts and audits[node]["candidate_sites"][i] != site else
                                        sum(min(value + l1(point, target) for value, target in zip(restricted[child], points[child])) for child in children[node])
                                        for i, point in enumerate(points[node])]
                if fixed_edges + sum(min(restricted[root]) for root in details["roots"]) == score:
                    best_common = site
                    break
        assert trials == details["common_site_trials"]
        if best_common is not None:
            assert set(record["hbt_sites"]) == {best_common}, "An equal-wirelength common candidate was missed"
        elif details.get("tie_merges"):
            assert policy == "coalesce"
        else:
            assert all(audits[node]["selected"] == canonical[node] for node in free), "No admissible coalescing tie was found, but the canonical solution changed"
        if "tie_merges" in details:
            previous = {node: audits[node]["candidate_sites"][canonical[node]] for node in hbts}
            last = canonical
            assert 0 <= details["pair_group_trials"] <= 256
            for step in details["tie_merges"]:
                forced = set(step["forced_nodes"])
                assert forced <= hbts and len(forced) == len(step["forced_nodes"])
                touched = {previous[node] for node in forced}
                assert forced == {node for node in hbts if previous[node] in touched}
                assert len(touched) == 2 or forced == hbts
                assert len(step["selected"]) == len(expected_order)
                chosen = dict(zip(expected_order, step["selected"]))
                positions = [node["position_dbu"][:] for node in nodes]
                for node in free:
                    assert 0 <= chosen[node] < len(points[node])
                    positions[node] = points[node][chosen[node]]
                for node in forced:
                    assert audits[node]["candidate_sites"][chosen[node]] == step["site"]
                assert sum(l1(positions[u], positions[v]) for u, v in edges) == score
                current = {node: audits[node]["candidate_sites"][chosen[node]] for node in hbts}
                assert len(set(current.values())) < len(set(previous.values()))
                previous, last = current, chosen
            assert all(audits[node]["selected"] == last[node] for node in free)
    elif policy == "first":
        assert all(audits[node]["selected"] == canonical[node] for node in free)
    assert continuous == details["continuous_positions_dbu"]
    continuous_score = sum(l1(continuous[u], continuous[v]) for u, v in edges)
    assert continuous_score == details["continuous_wirelength_dbu"] == fixed_edges + sum(continuous_min[root] for root in details["roots"])
    assert score >= continuous_score
    return len(audits), candidate_states, unfixed_leaves


def expanded_labels(record):
    labels = record["edge_dies"][:]
    planar, tree = record["planar"], record["expanded"]
    assert len(labels) == len(planar["edges"]) and all(die in (0, 1) for die in labels)
    for i, node in enumerate(planar["nodes"]):
        assert tree["nodes"][i]["position_dbu"] == node["position_dbu"]
        assert tree["nodes"][i]["pin_dies"] == ([] if i in record["hbt_nodes"] else node["pin_dies"])
    assert tree["edges"][:len(planar["edges"])] == planar["edges"]
    next_node = len(planar["nodes"])
    for node in record["hbt_nodes"]:
        assert not tree["nodes"][node]["pin_dies"]
        for die in (0, 1):
            multiplicity = planar["nodes"][node]["pin_dies"].count(die)
            if multiplicity:
                assert tree["edges"][len(labels)] == [node, next_node]
                assert tree["nodes"][next_node]["pin_dies"] == [die] * multiplicity
                assert tree["nodes"][next_node]["position_dbu"] == planar["nodes"][node]["position_dbu"]
                labels.append(die)
                next_node += 1
    assert len(labels) == len(tree["edges"]) and next_node == len(tree["nodes"])
    if "expanded_edge_dies" in record:
        assert labels == record["expanded_edge_dies"]
    return labels


def verify_partition(record):
    tree = record["expanded"]
    labels = expanded_labels(record)
    details = record["paper_decomposition"]
    root = details["root"]
    assert root == record["die_assignment_root"]
    adjacent = [[] for _ in tree["nodes"]]
    for u, v in tree["edges"]:
        adjacent[u].append(v)
        adjacent[v].append(u)
    postorder, stack = [], [(root, -1, False)]
    while stack:
        node, parent, exited = stack.pop()
        if exited:
            postorder.append(node)
            continue
        stack.append((node, parent, True))
        for child in reversed(adjacent[node]):
            if child != parent:
                stack.append((child, node, False))
    assert postorder == details["reverse_dfs_order"]
    assert sorted(postorder) == list(range(len(tree["nodes"])))
    graph = defaultdict(set)
    for (u, v), die in zip(tree["edges"], labels):
        graph[(die, u)].add((die, v))
        graph[(die, v)].add((die, u))
    for node, value in enumerate(tree["nodes"]):
        for die in value["pin_dies"]:
            graph[(die, node)]
    hbt_at = dict(zip(record["hbt_nodes"], record["hbt_sites"]))
    aliases = {}
    for node, site in hbt_at.items():
        for die in (0, 1):
            graph[(die, node)]
            if (die, site) in aliases:
                other = aliases[(die, site)]
                graph[(die, node)].add(other)
                graph[other].add((die, node))
            else:
                aliases[(die, site)] = (die, node)
    expected, visited = set(), set()
    for start in sorted(graph):
        if start in visited:
            continue
        visited.add(start)
        queue, members = deque([start]), set()
        while queue:
            current = queue.popleft()
            members.add(current[1])
            for child in graph[current]:
                if child not in visited:
                    visited.add(child)
                    queue.append(child)
        die = start[0]
        edge_ids = tuple(i for i, ((u, v), layer) in enumerate(zip(tree["edges"], labels)) if layer == die and u in members and v in members)
        sites = tuple(sorted({hbt_at[node] for node in members if node in hbt_at}))
        expected.add((die, tuple(sorted(members)), edge_ids, sites))
    actual = {(part["die"], tuple(sorted(part["nodes"])), tuple(sorted(part["edges"])), tuple(sorted(part["hbt_sites"])))
              for part in details["components"]}
    assert expected == actual and len(actual) == len(details["components"]), "Reverse-DFS split differs from independent die-copy connectivity"
    return len(actual)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--experiment", type=Path, required=True)
    parser.add_argument("--stage", default="small_routing")
    parser.add_argument("--compare-stage", help="Compare completed per-net traces against another run of the same seed")
    args = parser.parse_args()
    folder = args.experiment.resolve() / args.stage
    source_root = experiment_source(args.experiment.resolve())
    path = folder / "h3d_report.json"
    report = json.loads((path if path.exists() else folder / "h3d_failure_report.json").read_text())
    assert report["method_profile"] == "paper"
    expected_hashes = json.loads((args.experiment / "source_sha256.json").read_text())
    assert all(digest(source_root / name) == value for name, value in expected_hashes.items())
    assert report["hbt_capacity_policy"] == "sequential_currently_available_sites_only"
    for feature in ("full_domain_distance_transform", "future_site_reservation", "forced_single_site_budget_fallback"):
        assert report["method_features"][feature] is False
    for feature in ("continuous_regions_and_eight_directions", "all_directional_nearest_ties", "reverse_dfs_hbt_split", "seeded_leaf_and_variable_roots"):
        assert report["method_features"][feature] is True
    initial = json.loads((folder / "hbt_grid_initial.json").read_text())["sites"]
    sites = {item["id"]: item["position_dbu"] for item in initial}
    owners = {item["id"]: item["initial_owner"] for item in initial}
    config = report["physical_configuration"]
    spacing = config["gcell_pitch_dbu"]
    raw = def_data(source_root / "bottom.def")
    bounds = raw["die"]
    xs = [(x + min(x + spacing, bounds[2])) // 2 for x in range(bounds[0], bounds[2], spacing)]
    ys = [(y + min(y + spacing, bounds[3])) // 2 for y in range(bounds[1], bounds[3], spacing)]
    routing = {y * len(xs) + x: [px, py] for y, py in enumerate(ys) for x, px in enumerate(xs)}
    source_sides = defaultdict(set)
    for die in ("top", "bottom"):
        for name in def_data(source_root / (die + ".def"))["nets"]:
            source_sides[name].add(die)
    start = time.monotonic()
    completed, cross, node_tables, states, components, leaves, coalesced, saved = 0, 0, 0, 0, 0, 0, 0, 0
    assignment_trials, assignment_changes, assignment_increases, assignment_extra = 0, 0, 0, 0
    seen_names, seen_ids, failed_records = set(), set(), []
    for record in traces(folder / "routing_traces.jsonl"):
        if record["status"] != "completed":
            assert record["status"] == "failed"
            failed_records.append(record)
            continue
        assert record["name"] not in seen_names and record["id"] not in seen_ids
        seen_names.add(record["name"])
        seen_ids.add(record["id"])
        assert record["method_profile"] == "paper" and not record["site_budget_fallback"]
        assert "hbt_site_budget" not in record and "future_cross_die_reserve" not in record
        is_cross = len(source_sides[record["name"]]) == 2
        assert record["hbt_count_dp_applied"] == is_cross
        planar = record["planar"]
        assert len(record["hbt_sites"]) == len(record["hbt_nodes"]) == record["hbt_count"]
        assert independent_mask_dp(planar["nodes"], planar["edges"]) == record["hbt_count"]
        assert assignment_cost(planar["nodes"], planar["edges"], record["edge_dies"]) == record["hbt_count"]
        if is_cross:
            degree = [0] * len(planar["nodes"])
            for u, v in planar["edges"]:
                degree[u] += 1
                degree[v] += 1
            assert len(degree) == 1 or degree[record["die_assignment_root"]] == 1
            assert record["hbt_free_sites_before"] == sum(owner == -1 for owner in owners.values())
            if "paper_assignment_trials" in record:
                trials = record["paper_assignment_trials"]
                assert 1 <= len(trials) <= report["paper_assignment_trial_limit"]
                keys = []
                for trial in trials:
                    assert len(trial["edge_dies"]) == len(planar["edges"])
                    assert all(die in (0, 1) for die in trial["edge_dies"])
                    masks = [sum(1 << die for die in set(node["pin_dies"])) for node in planar["nodes"]]
                    for (u, v), die in zip(planar["edges"], trial["edge_dies"]):
                        masks[u] |= 1 << die
                        masks[v] |= 1 << die
                    assert sorted(trial["hbt_nodes"]) == [i for i, mask in enumerate(masks) if mask == 3]
                    assert len(trial["hbt_nodes"]) == record["hbt_count"]
                    assert 1 <= trial["physical_sites"] <= record["hbt_count"] and trial["wirelength_dbu"] >= 0
                    keys.append((trial["physical_sites"], trial["wirelength_dbu"]))
                winner = record["selected_assignment_trial"]
                assert winner == min(range(len(keys)), key=lambda i: (keys[i], i))
                assert trials[winner]["edge_dies"] == record["edge_dies"] and trials[winner]["hbt_nodes"] == record["hbt_nodes"]
                assert keys[winner] == (len(set(record["hbt_sites"])), record["optimized_wirelength_dbu"])
                assignment_trials += len(trials)
                assignment_changes += winner != 0
                if keys[winner][1] > keys[0][1]:
                    assert keys[winner][0] < keys[0][0]
                    assignment_increases += 1
                    assignment_extra += keys[winner][1] - keys[0][1]
            tables, values, extra = verify_location(record, sites, owners, routing)
            node_tables += tables
            states += values
            leaves += extra
            before = record["paper_location"].get("sites_before_tie_break", len(set(record["hbt_sites"])))
            reduction = before - len(set(record["hbt_sites"]))
            assert reduction >= 0
            coalesced += reduction > 0
            saved += reduction
            cross += 1
        else:
            assert record["hbt_count"] == 0 and not record["hbt_nodes"] and "paper_location" not in record
            assert record["positions_dbu"] == [node["position_dbu"] for node in record["expanded"]["nodes"]]
        components += verify_partition(record)
        for node, site in zip(record["hbt_nodes"], record["hbt_sites"]):
            assert owners[site] == -1 and record["positions_dbu"][node] == sites[site]
        for site in set(record["hbt_sites"]):
            owners[site] = record["id"]
        completed += 1
    assert completed == report["completed_input_nets"] == len(report["nets"])
    assert seen_names == {net["name"] for net in report["nets"]}
    assert cross == sum(net["cross_die"] for net in report["nets"])
    assert len(failed_records) == (report["status"] != "completed")
    result = {"status": "passed_public_paper_method_checks_on_completed_scope", "native_run_status": report["status"],
              "completed_original_nets_checked": completed, "cross_die_location_problems_checked": cross,
              "eq6_regions_and_eq7_tables_checked": node_tables, "candidate_state_costs_checked": states,
              "reverse_dfs_components_checked": components, "unfixed_variable_leaf_extension_cases": leaves,
              "hbt_conflicts": 0, "remaining_free_sites": sum(owner == -1 for owner in owners.values()),
              "all_closest_ties_rule": "explicit engineering choice; paper does not specify tie handling",
              "rng_rule": "recorded seed and valid leaf/movable roots; author's RNG sequence is unavailable",
              "extra_full_domain_or_budget_optimizations": False, "wall_seconds": time.monotonic() - start}
    result["zero_cost_coalesced_nets"] = coalesced
    result["sites_saved_by_equal_wirelength_ties_in_this_run"] = saved
    result["equivalent_quantity_assignments_checked"] = assignment_trials
    result["noncanonical_quantity_assignments_selected"] = assignment_changes
    result["quantity_tie_choices_with_more_wirelength_for_fewer_sites"] = assignment_increases
    result["quantity_tie_extra_wirelength_vs_first_assignment_dbu"] = assignment_extra
    result["quantity_assignment_trial_limit"] = report.get("paper_assignment_trial_limit", 1)
    result["quantity_assignment_search_enabled"] = report.get("paper_assignment_trial_limit", 1) > 1
    if args.compare_stage:
        other = args.experiment.resolve() / args.compare_stage
        comparison_path = other / "h3d_report.json"
        comparison_report = json.loads((comparison_path if comparison_path.exists() else other / "h3d_failure_report.json").read_text())
        assert comparison_report["random_seed"] == report["random_seed"]
        assert comparison_report["method_profile"] == "paper"
        assert comparison_report.get("paper_assignment_trial_limit", 1) == report.get("paper_assignment_trial_limit", 1)
        assert comparison_report.get("paper_traceback_ties", "first") == report.get("paper_traceback_ties", "first")
        selection_difference = report["selected_cross_die_net_count"] - comparison_report["selected_cross_die_net_count"]
        def completed_traces(path):
            return (record for record in traces(path / "routing_traces.jsonl") if record["status"] == "completed")
        compared = 0
        for current, previous in itertools.zip_longest(completed_traces(folder), completed_traces(other)):
            assert current is not None and previous is not None, "Completed-prefix lengths differ"
            # A prefix selection contains fewer future nets. Paper mode records this
            # counter for observation only; it never uses it as a site reservation.
            assert current.pop("remaining_cross_die_nets_after") - previous.pop("remaining_cross_die_nets_after") == selection_difference
            assert current == previous, "Completed-prefix algorithm traces differ: " + str(sorted(key for key in current.keys() | previous.keys() if current.get(key) != previous.get(key)))
            compared += 1
        assert compared == completed
        result["completed_trace_replay"] = {"comparison_stage": args.compare_stage, "identical_algorithm_records": compared,
                                           "verified_future_net_counter_offset": selection_difference}
    if report["status"] != "completed":
        result["full_design_completed"] = False
        result["failed_net"] = report["failed_net"]
        result["failed_stage"] = report["failed_stage"]
        result["failure_reason"] = report["error"]
        assert failed_records[0]["name"] == report["failed_net"]
        assert failed_records[0]["stage"] == report["failed_stage"]
        assert failed_records[0]["error"] == report["error"]
    else:
        result["full_design_completed"] = report["completed_input_nets"] == report["original_net_count"]
    (folder / "paper_method_verification.json").write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps(result, indent=2))


if __name__ == "__main__":
    main()
