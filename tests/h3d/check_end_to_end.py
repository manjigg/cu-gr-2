#!/usr/bin/env python3
"""Validate H3D outputs independently, including joint connectivity and DEF re-import."""
import argparse
import copy
import hashlib
import json
from pathlib import Path
import re
import subprocess

from check_fixture import check_guides

ROOT = Path(__file__).resolve().parents[2]
FIXTURE = ROOT / "tests/data/h3d/tiny"


def section(text, name):
    match = re.search(r"^\s*" + name + r"\s+(\d+)\s*;(.*?)\bEND\s+" + name + r"\b", text, re.M | re.S)
    if match is None:
        raise ValueError("Missing DEF section " + name)
    entries = re.findall(r"-\s+(\S+)\s+(.*?);", match[2], re.S)
    if len(entries) != int(match[1]) or len(dict(entries)) != len(entries):
        raise ValueError("Wrong count or duplicate names in DEF section " + name)
    return dict(entries)


def nets(text):
    return {name: [(re.sub(r"\\(.)", r"\1", inst), pin) for inst, pin in re.findall(r"\(\s*(\S+)\s+(\S+)\s*\)", body)]
            for name, body in section(text, "NETS").items()}


def run(command, log_path, expected_success=True):
    result = subprocess.run([str(item) for item in command], cwd=ROOT / "flute",
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, timeout=60)
    log_path.write_text(result.stdout)
    if (result.returncode == 0) != expected_success:
        raise RuntimeError("Unexpected exit {}: see {}".format(result.returncode, log_path))
    return result.stdout


class Connectivity:
    def __init__(self):
        self.parents = {}

    def root(self, node):
        if node not in self.parents:
            self.parents[node] = node
        if self.parents[node] != node:
            self.parents[node] = self.root(self.parents[node])
        return self.parents[node]

    def connect(self, a, b):
        self.parents[self.root(a)] = self.root(b)


def validate_outputs(directory, config, binary, reimport, inputs):
    report = json.loads((directory / "h3d_report.json").read_text())
    assert report["connectivity_checked"] is True
    for item in report["pad_capacity_sync"].values():
        assert item["demand_preserved"] and item["matches_fresh_final_geometry"]
        assert item["capacity_reduction"] >= -1e-9
    connectivity = Connectivity()
    endpoint_subnet = {}
    ports = {}
    generated_nets = {}
    source_groups = {}
    expected_groups = {}
    for die in ("top", "bottom"):
        original_text = inputs[die].read_text()
        output_text = (directory / (die + ".def")).read_text()
        assert section(original_text, "COMPONENTS") == section(output_text, "COMPONENTS")
        ports[die] = section(output_text, "PINS")
        generated_nets[die] = nets(output_text)
        expected_names = {net["name"] for net in report["subnets"] if net["die"] == die}
        assert set(generated_nets[die]) == expected_names
        check_guides(directory / (die + ".guide"), expected_names)
        for name, endpoints in generated_nets[die].items():
            connectivity.root((die, name))
            assert endpoints
            for instance, pin in endpoints:
                key = (die, instance, pin)
                assert key not in endpoint_subnet, "A physical terminal occurs on two subnets"
                endpoint_subnet[key] = (die, name)
                if instance == "PIN":
                    match = re.search(r"\+\s*NET\s+(\S+)", ports[die][pin])
                    assert match and match[1] == name, "PINS.NET disagrees with NETS connection"
        for name, endpoints in nets(original_text).items():
            global_name = config.get("net_aliases", {}).get(die, {}).get(name, name)
            source_groups.setdefault(global_name, []).extend((die, inst, pin) for inst, pin in endpoints)
            expected_groups.setdefault(global_name, set()).add(die)
        if reimport:
            guide = directory / (die + ".reimport.guide")
            run([binary, "-lef", FIXTURE / "technology.lef", "-def", directory / (die + ".def"),
                 "-output", guide], directory / (die + ".reimport.log"))
            check_guides(guide, expected_names)

    hbts = report["hbts"]
    assert len(hbts) == report["physical_hbt_count"]
    assert len({hbt["site"] for hbt in hbts}) == len(hbts), "Two nets occupy the same HBT"
    assert report["remaining_available_hbt_sites"] == report["initial_available_hbt_sites"] - len(hbts)
    ids = {net["id"]: net["name"] for net in report["nets"]}
    for hbt in hbts:
        site = hbt["site"]
        ix, iy = site % config["site_counts"][0], site // config["site_counts"][0]
        assert [ix, iy] not in config["blocked_sites"]
        expected_position = [config["origin_dbu"][0] + ix * config["pitch_dbu"][0],
                             config["origin_dbu"][1] + iy * config["pitch_dbu"][1]]
        assert hbt["position_dbu"] == expected_position
        connected = []
        for die in ("top", "bottom"):
            name = hbt[die + "_port"]
            body = ports[die][name]
            layer = re.search(r"\+\s*LAYER\s+(\S+)", body)
            position = re.search(r"\+\s*FIXED\s*\(\s*(-?\d+)\s+(-?\d+)\s*\)", body)
            assert layer and layer[1] == config["landing_layers"][die]
            assert position and list(map(int, position.groups())) == expected_position
            subnet = endpoint_subnet[(die, "PIN", name)]
            metadata = next(item for item in report["subnets"] if (item["die"], item["name"]) == subnet)
            assert metadata["original_net_id"] == hbt["owner_net_id"]
            connected.append(subnet)
        connectivity.connect(*connected)

    group_roots = set()
    for name, endpoints in source_groups.items():
        roots = {connectivity.root(endpoint_subnet[endpoint]) for endpoint in endpoints}
        assert len(roots) == 1, "An original net is disconnected across dies: " + name
        root = roots.pop()
        assert root not in group_roots, "Two original nets were shorted together"
        group_roots.add(root)
    assert set(ids.values()) == set(source_groups)
    assert report["cross_die_net_count"] == sum(len(dies) == 2 for dies in expected_groups.values())
    for subnet in report["subnets"]:
        assert subnet["pins"] == [list(pair) for pair in generated_nets[subnet["die"]][subnet["name"]]]
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--route", type=Path, default=ROOT / "build/route")
    parser.add_argument("--output-dir", type=Path, default=ROOT / "build/h3d_e2e_check")
    args = parser.parse_args()
    binary = args.route.resolve()
    output = args.output_dir.resolve()
    output.mkdir(parents=True, exist_ok=True)
    base = json.loads((FIXTURE / "hbt.json").read_text())

    def execute(name, config, expected_success=True, reimport=False, congested=False, no_ports=False, nested_ports=False,
                expected_error="No available HBT/location candidate", method="enhanced", escaped_instances=False):
        folder = output / name
        folder.mkdir(parents=True, exist_ok=True)
        config_path = folder / "hbt.json"
        config_path.write_text(json.dumps(config, indent=2))
        inputs = {die: FIXTURE / (die + ".def") for die in ("top", "bottom")}
        if congested or no_ports or nested_ports or escaped_instances:
            for die in inputs:
                text = inputs[die].read_text()
                if escaped_instances:
                    text = text.replace("U0", r"U\\0")
                if congested:
                    text = re.sub(r"TRACKS ([XY]) 200 DO 63 STEP 400", r"TRACKS \1 2100 DO 6 STEP 4200", text)
                if no_ports:
                    text = re.sub(r"\bPINS\s+\d+\s*;.*?\bEND PINS", "", text, flags=re.S)
                    text = re.sub(r"\(\s*PIN\s+\S+\s*\)", "", text)
                if nested_ports:
                    text = re.sub(r"\+\s*LAYER\s+M1", "+ PORT\n  + LAYER M1", text)
                inputs[die] = folder / (die + ".input.def")
                inputs[die].write_text(text)
        log = run([binary, "-lef", FIXTURE / "technology.lef", "-top-def", inputs["top"],
                   "-bottom-def", inputs["bottom"], "-hbt-config", config_path,
                    "-output-dir", folder, "-h3d-method", method], folder / "route.log", expected_success)
        if not expected_success:
            assert expected_error in log
            return None
        report = validate_outputs(folder, config, binary, reimport, inputs)
        if congested:
            assert "stage 2: pattern routing" in log and "stage 3: maze routing" in log
        print("{}: {} original nets, {} cross-die nets, {} HBTs; connectivity/DEF/guide checks passed".format(
            name, report["original_net_count"], report["cross_die_net_count"], report["physical_hbt_count"]))
        return report

    first = execute("baseline", base, reimport=True)
    assert sum(item["changed_edges"] for item in first["pad_capacity_sync"].values()) > 0
    assert all(item["overflow_net_count"] == 0 for item in first["routing"].values())
    repeated = execute("repeat", base)
    def deterministic_fields(report):
        result = copy.deepcopy(report)
        result.pop("phase_seconds", None)
        result.pop("total_seconds", None)
        return result
    assert deterministic_fields(first) == deterministic_fields(repeated), "H3D results are not deterministic"

    merged = copy.deepcopy(base)
    merged.update(origin_dbu=[9000, 9000], site_counts=[1, 1], blocked_sites=[],
                  net_aliases={"bottom": {"cross_b": "bottom_cross_b", "cross_c": "bottom_cross_c"}})
    report = execute("same_net_merge", merged, reimport=True)
    assert report["cross_die_net_count"] == 1 and report["physical_hbt_count"] == 1
    assert report["topological_hbt_count"] > report["physical_hbt_count"], "Merge case did not exercise HBT merging"

    budget = copy.deepcopy(base)
    budget.update(origin_dbu=[9000, 9000], site_counts=[2, 1], blocked_sites=[],
                  net_aliases={"bottom": {"cross_b": "bottom_cross_b"}})
    report = execute("two_net_budget", budget, reimport=True)
    assert report["cross_die_net_count"] == 2 and report["physical_hbt_count"] == 2
    assert all(item["physical_hbt_count"] == 1 for item in report["nets"] if item["cross_die"])
    assert any(item["site_budget_fallback"] for item in report["nets"]), "Resource regression did not exercise budgeted coalescing"

    local = copy.deepcopy(base)
    local["net_aliases"] = {"bottom": {name: "bottom_" + name for name in ("cross_a", "cross_b", "cross_c")}}
    report = execute("local_only", local)
    assert report["physical_hbt_count"] == 0 and report["cross_die_net_count"] == 0

    execute("congested", base, congested=True)
    execute("no_original_ports", base, no_ports=True, reimport=True)
    execute("nested_ports", base, nested_ports=True, reimport=True)

    paper = execute("paper_subnet_routing", base, method="paper", reimport=True)
    assert paper["method_features"]["rebuild_single_die_subnet_rsmt"]
    assert paper["method_features"]["subnet_physical_pin_access"]
    execute("escaped_instance_names", base, method="paper", escaped_instances=True, reimport=True)
    compact = copy.deepcopy(base)
    compact["write_routing_traces"] = False
    execute("paper_no_trace", compact, method="paper")
    assert not (output / "paper_no_trace/routing_traces.jsonl").exists()
    for name in ("top.def", "bottom.def", "top.guide", "bottom.guide"):
        assert hashlib.sha256((output / "paper_no_trace" / name).read_bytes()).digest() == hashlib.sha256((output / "paper_subnet_routing" / name).read_bytes()).digest(), "Tracing changed routing output"
    projected = copy.deepcopy(base)
    projected["hbt_optimization_coordinates"] = "gcell_centers"
    execute("paper_grid_coordinates", projected, method="paper", reimport=True)
    grid = json.loads((output / "paper_grid_coordinates/hbt_grid_initial.json").read_text())
    assert any(s["position_dbu"] != s["physical_position_dbu"] for s in grid["sites"]), "Projection fixture must move grid representatives"
    # validate_outputs above independently checks every emitted pad against the
    # unprojected config, so an optimization-coordinate leak into DEF fails.
    collapsed = copy.deepcopy(projected)
    collapsed.update(pitch_dbu=[1000, 1000], blocked_sites=[])
    execute("colliding_grid_projection", collapsed, method="paper", expected_success=False,
            expected_error="HBT sites do not map injectively to the uniform grid")

    exhausted = copy.deepcopy(base)
    exhausted["blocked_sites"] = [[x, y] for y in range(4) for x in range(4)]
    execute("exhausted", exhausted, expected_success=False,
            expected_error="Insufficient HBT sites for the cross-die-net lower bound")
    bad_spacing = copy.deepcopy(base)
    bad_spacing["pad_size_dbu"] = [5900, 5900]
    execute("invalid_spacing", bad_spacing, expected_success=False,
            expected_error="HBT pitch is smaller than pad size plus required spacing")
    print("Exhaustion returned an error; no illegal HBT reuse. All end-to-end checks passed.")


if __name__ == "__main__":
    main()
