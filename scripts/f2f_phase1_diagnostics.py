#!/usr/bin/env python3
"""Summarize the per-net/candidate evidence emitted by F2F diagnostics."""
from collections import Counter
from pathlib import Path
import argparse
import gzip
import json


def read_events(path):
    opener = gzip.open if path.suffix == ".gz" else open
    with opener(path, "rt") as stream:
        for line in stream:
            yield json.loads(line)


def wire(route):
    return sum(die["wire_um"] for die in route["dies"])


def summarize(path):
    nets, stages, counts = {}, {}, Counter()
    stats = Counter()
    pending = {}
    examples = {}
    for event in read_events(path):
        kind = event["type"]
        if kind == "net":
            nets[event["net"]] = event
            continue
        stage = event["stage"]
        if kind == "stage":
            stages[stage] = event["dies"]
            continue
        net = event["net"]
        key = (stage, net)
        if kind == "candidate":
            counts[f"s{stage}_candidates"] += 1
            if not event["feasible"]:
                counts[f"s{stage}_infeasible_candidates"] += 1
                continue
            group = pending.setdefault(key, {})
            hbt = len(event["sites"])
            size = wire(event["metrics"])
            if hbt not in group or size < group[hbt][0]:
                group[hbt] = (size, event["metrics"]["cost"], event["sites"])
        elif kind == "search":
            for key_name in ("single_evaluated", "multi_evaluated", "empty_site_lists", "incomplete_combinations",
                             "expansions", "retained_dp_topologies", "beam_collision_rejections"):
                counts[f"s{stage}_{key_name}"] += event.get(key_name, 0)
        elif kind == "decision":
            counts[f"s{stage}_{event['action']}"] += 1
            group = pending.pop(key, {})
            if event["action"] != "accept":
                continue
            chosen = event["new"]
            chosen_wire = wire(chosen)
            hbt = len(chosen["sites"])
            for die in chosen["dies"]:
                stats[f"s{stage}_duplicate_wire_um"] += die["duplicate_wire_um"]
                counts[f"s{stage}_duplicate_vias"] += die["duplicate_vias"]
            projected = nets[net]["projected_rsmt_um"]
            if stage == 1 and all(nets[net]["pins"]) and projected is not None:
                stats["s1_cross_die_projected_um"] += projected
                stats["s1_cross_die_routed_um"] += chosen_wire
            previous = event.get("old")
            if previous is not None:
                delta = chosen_wire - wire(previous)
                stats[f"s{stage}_accepted_wire_delta_um"] += delta
                counts[f"s{stage}_accepted_higher_cost"] += chosen["cost"] > previous["cost"] + 1e-6
                counts[f"s{stage}_accepted_longer_wire"] += delta > 1e-6
            for alternative, reason in ((hbt, "same_count_shorter"), (hbt - 1, "fewer_hbt_shorter"),
                                        (hbt + 1, "more_hbt_shorter")):
                option = group.get(alternative)
                if option is None or option[0] + 1e-6 >= chosen_wire:
                    continue
                counts[f"s{stage}_{reason}"] += 1
                delta = chosen_wire - option[0]
                stats[f"s{stage}_{reason}_um"] += delta
                name = f"s{stage}_{reason}"
                if name not in examples or delta > examples[name]["delta_wire_um"]:
                    examples[name] = dict(net=nets[net], delta_wire_um=delta,
                                          selected_sites=chosen["sites"], alternative_sites=option[2],
                                          selected_cost=chosen["cost"], alternative_cost=option[1])
    return dict(nets=len(nets), stages=stages, counters=counts, lengths=stats, examples=examples)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", type=Path)
    args = parser.parse_args()
    print(json.dumps(summarize(args.input), indent=2, sort_keys=True))
