#!/usr/bin/env python3
"""Measure same-cost HBT coalescing opportunities on frozen paper candidate tables."""
import argparse
from collections import Counter
import json
from pathlib import Path

from validate_h3d_aes import traces, l1


def common_site_cost(record, common):
    nodes = record["expanded"]["nodes"]
    audits = {row["node"]: row for row in record["paper_location"]["nodes"]}
    points, values = {}, {}
    for node, entry in enumerate(nodes):
        if entry["pin_dies"]:
            points[node], values[node] = [entry["position_dbu"]], [0]
    for node in reversed(record["paper_location"]["bfs_order"]):
        row = audits[node]
        ids = [row["candidate_sites"].index(common)] if row["domain"] == "hbt" else range(len(row["candidate_sites"]))
        # Reconstruct candidate coordinates from the regular AES grids.
        def point(site):
            if row["domain"] == "hbt":
                return [3000 + 6000 * (site % 42), 3000 + 6000 * (site // 42)]
            x, y = site % 60, site // 60
            return [(4200 * x + min(4200 * (x + 1), 250000)) // 2,
                    (4200 * y + min(4200 * (y + 1), 250000)) // 2]
        points[node] = [point(row["candidate_sites"][i]) for i in ids]
        values[node] = [sum(min(cost + l1(p, q) for cost, q in zip(values[child], points[child])) for child in row["children"])
                        for p in points[node]]
    fixed = sum(l1(nodes[u]["position_dbu"], nodes[v]["position_dbu"]) for u, v in record["expanded"]["edges"]
                if nodes[u]["pin_dies"] and nodes[v]["pin_dies"])
    return fixed + sum(min(values[root]) for root in record["paper_location"]["roots"])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--traces", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    histogram, examples, candidates, eligible = Counter(), [], 0, 0
    grid = json.loads((args.traces.parent / "hbt_grid_initial.json").read_text())["sites"]
    capacity = sum(site["initial_owner"] == -1 for site in grid)
    used, first_deficit, completed_cross = set(), None, 0
    for record in traces(args.traces):
        if record["status"] != "completed":
            continue
        used.update(record["hbt_sites"])
        completed_cross += bool(record["hbt_sites"])
        lower_bound = len(used) + record["remaining_cross_die_nets_after"]
        if lower_bound > capacity and first_deficit is None:
            first_deficit = {"net": record["name"], "committed_sites": len(used),
                             "completed_cross_die_nets": completed_cross,
                             "remaining_cross_die_nets": record["remaining_cross_die_nets_after"],
                             "minimum_sites_with_current_commitments": lower_bound,
                             "capacity": capacity, "deficit": lower_bound - capacity}
        if len(set(record["hbt_sites"])) < 2:
            continue
        histogram[len(set(record["hbt_sites"]))] += 1
        rows = [row for row in record["paper_location"]["nodes"] if row["domain"] == "hbt"]
        common = set.intersection(*(set(row["candidate_sites"]) for row in rows))
        candidates += len(common)
        if not common:
            continue
        eligible += 1
        best = min((common_site_cost(record, site), site) for site in sorted(common))
        assert best[0] >= record["optimized_wirelength_dbu"]
        examples.append({"name": record["name"], "physical_sites_before": len(set(record["hbt_sites"])),
                         "candidate_intersection_size": len(common), "common_site": best[1],
                         "common_site_wirelength_dbu": best[0], "original_wirelength_dbu": record["optimized_wirelength_dbu"],
                         "gap_dbu": best[0] - record["optimized_wirelength_dbu"]})
    zero = [item for item in examples if item["gap_dbu"] == 0]
    mandatory_multi = sum(histogram.values()) - len(zero)
    report_path = args.traces.parent / "h3d_report.json"
    if not report_path.exists():
        report_path = args.traces.parent / "h3d_failure_report.json"
    selected_cross = json.loads(report_path.read_text())["selected_cross_die_net_count"]
    result = {"scope": "fixed historical occupancy and paper candidate sets; not a new full-design run",
              "multi_site_nets": sum(histogram.values()), "site_count_histogram": dict(histogram),
              "nets_with_common_candidate": eligible, "common_site_trials": candidates,
              "same_wirelength_single_site_nets": len(zero),
              "potential_sites_saved_without_wirelength_increase": sum(item["physical_sites_before"] - 1 for item in zero),
              "first_infeasible_current_commitment": first_deficit,
              "capacity_bound_scope": "Each remaining cross-die net needs at least one distinct site; bound fixes current allocations and does not prove global infeasibility.",
              "nets_requiring_multiple_sites_at_recorded_minimum_cost": mandatory_multi,
              "frozen_candidate_minimum_total_sites_lower_bound": selected_cross + mandatory_multi,
              "frozen_candidate_capacity_deficit_lower_bound": max(0, selected_cross + mandatory_multi - capacity),
              "frozen_bound_scope": "Recorded candidate sets and their individual minimum costs are fixed; changing earlier assignments or recomputing domains can change this bound.",
              "examples": examples}
    args.out.write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps({key: value for key, value in result.items() if key != "examples"}, indent=2))


if __name__ == "__main__":
    main()
