#!/usr/bin/env python3
"""Read-only AES provenance/resource audit and explicitly labeled controls."""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import tarfile
from urllib.request import urlopen

from h3d_aes_experiment import ROOT, SOURCE, def_data, digest
from validate_h3d_aes import traces, independent_mask_dp

UPSTREAM_SHA = "e6d23148beed101b7741960f8aa967544259b0df"


def summarize(out):
    cases = {}
    for name in ("original_3um_paper", "paper_2um_control"):
        folder = out / name
        path = folder / "h3d_report.json"
        report = json.loads((path if path.exists() else folder / "h3d_failure_report.json").read_text())
        cases[name] = {key: report[key] for key in ("status", "completed_input_nets", "method_profile", "method_features", "routing", "hbt_resource_audit")}
        for key in ("failure_category", "failed_net", "failed_stage", "hbt_free_sites_at_failure", "physical_hbt_count", "remaining_available_hbt_sites"):
            if key in report:
                cases[name][key] = report[key]
        cases[name]["physical_configuration"] = report["physical_configuration"]
        cases[name]["process"] = json.loads((folder / "process_status.json").read_text())
        cases[name]["method_verification"] = json.loads((folder / "paper_method_verification.json").read_text())
    old, dense = (cases[name] for name in ("original_3um_paper", "paper_2um_control"))
    changed = sorted(key for key in old["physical_configuration"].keys() | dense["physical_configuration"].keys()
                     if old["physical_configuration"].get(key) != dense["physical_configuration"].get(key))
    assert changed == ["_experiment_scope", "pitch_dbu", "site_counts"]
    assert old["method_features"] == dense["method_features"]
    assert not dense["method_features"]["full_domain_distance_transform"] and not dense["method_features"]["future_site_reservation"]
    probes, sources = {}, {}
    for filename in ("probe_paper_paired_3um.json", "probe_full_domain_3um.json", "probe_full_domain_reserved_3um.json", "probe_non_io_3um.json"):
        value = json.loads((out / filename).read_text())
        argv = value["argv"]
        source_path = ROOT / argv[1]
        if source_path not in sources:
            sources[source_path] = {record["name"]: record for record in traces(source_path)}
        grid = json.loads((ROOT / argv[2]).read_text())["sites"]
        owners = {site["id"]: site["initial_owner"] for site in grid}
        remaining = value["selected_cross_die_nets"]
        for row in value["nets"]:
            source = sources[source_path][row["name"]]
            assert independent_mask_dp(source["nodes"], source["edges"]) == row["topological_hbts"]
            assert row["free_sites_before"] == sum(owner == -1 for owner in owners.values())
            assert len(row["sites"]) == row["topological_hbts"]
            if value["domain"] == "paired":
                assert row["sites"] == row["paper_sites"]
                assert row["full_wirelength_dbu"] <= row["paper_wirelength_dbu"]
                assert all(owners[site] == -1 for site in row["full_sites"])
            assert all(owners[site] == -1 for site in row["sites"])
            for site in set(row["sites"]):
                owners[site] = row["id"]
            assert row["free_sites_after"] == sum(owner == -1 for owner in owners.values())
            remaining -= 1
            if value["capacity_policy"] == "reserve":
                assert row["free_sites_after"] >= remaining
        assert remaining == value["remaining_cross_die_nets"]
        assert len(value["nets"]) == value["completed_cross_die_nets"]
        if value["status"] == "failed":
            assert value["free_sites_at_failure"] == 0
        probes[filename] = {key: item for key, item in value.items() if key != "nets"}
        probes[filename]["independent_claim_and_quantity_check"] = "passed"
    paired = probes["probe_paper_paired_3um.json"]
    assert paired["completed_original_nets"] == old["completed_input_nets"] and paired["failed_net"] == old["failed_net"]
    assert old["failure_category"] == "hbt_resource_exhaustion" and old["hbt_free_sites_at_failure"] == 0
    output_check = json.loads((out / "paper_2um_control/independent_verification.json").read_text())
    witness = json.loads((out / "existing_3um_feasible_witness_verification.json").read_text())
    assert output_check["processed_nets_checked"] == witness["processed_nets_checked"] == 14310
    assert output_check["conflicts"] == witness["conflicts"] == 0
    summary = {"status": "controlled_root_cause_identified_original_3um_paper_still_exhausts",
               "root_cause": "Sequential local wirelength minimization commits more HBT sites than the 3 um grid can supply to remaining nets; candidate pruning is not a necessary cause.",
               "data_conclusion": "Converted endpoint data is internally consistent and a same-grid full routing witness exists; the proposed grid is not an authenticated original placer resource specification.",
               "native_cases": cases, "controls_changed_configuration_keys": changed, "allocation_only_probes": probes,
               "original_3um_full_feasibility_witness": witness,
               "two_um_full_output_verification": output_check,
               "two_um_is_original_three_um_repair": False,
               "source_original_files_unchanged": all(digest(SOURCE / name) == value for name, value in json.loads((out / "source_sha256.json").read_text()).items()),
               "core_source_sha256": {str(path.relative_to(ROOT)): digest(path) for path in sorted((ROOT / "src/h3d").glob("*.[ch]*")) if path.is_file()},
               "converter_sha256": digest(ROOT / "benchmarks/h3d_aes_convert.py"), "route_binary_sha256": digest(ROOT / "build/route")}
    summary["data_provenance_audit"] = {"file": "data_provenance_audit.json", "sha256": digest(out / "data_provenance_audit.json")}
    enumeration = [{"first_hbt_x": x, "second_hbt_x": y, "wirelength": 2 * abs(x - 20) + abs(x - y) + 2 * abs(y - 40),
                    "distinct_sites": len({x, y})} for x in (20, 40) for y in (20, 40)]
    summary["minimal_counterexample"] = {"first_net_hpwl": 20, "second_net_hpwl": 60, "available_site_x": [20, 40],
        "first_net_assignments_exhaustively_enumerated": enumeration,
        "unique_local_wirelength_optimum": min(item["wirelength"] for item in enumeration),
        "best_one_site_wirelength": min(item["wirelength"] for item in enumeration if item["distinct_sites"] == 1),
        "conclusion": "Even all legal candidates plus exact Eq7 consume both sites for the first net; a feasible two-net solution requires abandoning that unique local minimum."}
    readback = out / "paper_2um_readback/process_status.json"
    if readback.exists():
        summary["two_um_native_readback"] = json.loads(readback.read_text())
    repeated = out / "paper_2um_repeat"
    if repeated.exists():
        same = {name: digest(out / "paper_2um_control" / name) == digest(repeated / name)
                for name in ("top.def", "bottom.def", "top.guide", "bottom.guide", "routing_traces.jsonl")}
        assert all(same.values())
        summary["two_um_repeatability"] = {"files_sha256_equal": same, "process_status": json.loads((repeated / "process_status.json").read_text())}
    (out / "summary.json").write_text(json.dumps(summary, indent=2) + "\n")
    print(json.dumps({"root_cause": summary["root_cause"], "original_3um_status": old["status"],
                      "two_um_control_status": dense["status"], "two_um_sites": dense["physical_hbt_count"],
                      "two_um_routing": dense["routing"], "source_original_files_unchanged": summary["source_original_files_unchanged"]}, indent=2))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--experiment", type=Path, required=True)
    parser.add_argument("--phase", choices=["data", "summary"], default="data")
    args = parser.parse_args()
    out = args.experiment.resolve()
    if args.phase == "summary":
        summarize(out)
        return
    audit = json.loads((out / "input_audit.json").read_text())
    manifest = json.loads((SOURCE / "3d_nets.json").read_text())["nets"]
    config = json.loads((out / "derived/hbt_native_pad_014um.json").read_text())
    hashes = json.loads((out / "source_sha256.json").read_text())
    assert all(digest(SOURCE / name) == value for name, value in hashes.items())
    archive_matches = {}
    with tarfile.open(ROOT / "h3d_aes_complete_package.tar.gz") as archive:
        packaged_converter = archive.extractfile("h3d_aes_convert.py").read()
        for name in hashes:
            archive_matches[name] = hashlib.sha256(archive.extractfile("h3d_aes_input/" + name).read()).hexdigest() == hashes[name]
    assert all(archive_matches.values())
    data = {die: def_data(SOURCE / (die + ".def")) for die in ("top", "bottom")}
    connections = {}
    for die, value in data.items():
        for name, pins in value["nets"].items():
            connections.setdefault(name, []).extend((die, instance, pin) for instance, pin in pins)
    assert len(connections) == audit["original_net_count"]
    for net in manifest:
        expected = [(die, pin["instance"], pin["pin"]) for die in ("top", "bottom") for pin in net[die + "_pins"]]
        assert Counter(expected) == Counter(connections[net["original_net_name"]])
    prior = [(net["original_net_name"], net["prior_terminal_reference_dbu"]) for net in manifest if "prior_terminal_reference_dbu" in net]
    io_created, io_internal_cross = 0, 0
    io_internal_names = []
    for net in manifest:
        if not net["contains_external_io"]:
            continue
        internal_sides = {die for die in ("top", "bottom") for pin in net[die + "_pins"] if pin["instance"] != "PIN"}
        io_created += internal_sides == {"top"}
        io_internal_cross += internal_sides == {"top", "bottom"}
        if internal_sides == {"top", "bottom"}:
            io_internal_names.append(net["original_net_name"])
    minimum, close3, close2, closest = None, 0, 0, None
    for i, (name, p) in enumerate(prior):
        for other, q in prior[:i]:
            distance = max(abs(p[0] - q[0]), abs(p[1] - q[1]))
            close3 += distance < 6000
            close2 += distance < 4000
            if minimum is None or distance < minimum:
                minimum, closest = distance, [name, other]
    # Download only reference source text, never execute upstream code.
    upstream = out / "upstream_sources"
    upstream.mkdir(exist_ok=True)
    refs = []
    for relative in ("Place-LoL/README.md", "Place-LoL/dreamplace/convert_common.py"):
        url = f"https://raw.githubusercontent.com/lamda-bbo/Open3DBench/{UPSTREAM_SHA}/{relative}"
        with urlopen(url, timeout=30) as response:
            content = response.read()
        filename = relative.replace("/", "__")
        target = upstream / filename
        if target.exists() and target.read_bytes() != content:
            raise ValueError("Pinned upstream source differs from the saved artifact: " + filename)
        if not target.exists():
            target.write_bytes(content)
        refs.append({"url": url, "saved_file": filename, "sha256": hashlib.sha256(content).hexdigest()})
    capacity = config["site_counts"][0] * config["site_counts"][1]
    report = {"input_internal_consistency": "passed", "matches_provided_archive_sha256": archive_matches,
              "original_nets": len(connections), "original_endpoints": sum(map(len, connections.values())),
              "cross_die_nets": len(manifest), "cross_die_with_io_assumption": sum(net["contains_external_io"] for net in manifest),
              "cross_die_without_external_io": sum(not net["contains_external_io"] for net in manifest),
              "io_cross_nets_created_only_by_bottom_port_convention": io_created,
              "io_nets_already_cross_die_without_ports": io_internal_cross,
              "io_nets_already_cross_die_without_ports_names": io_internal_names,
              "diearea_dbu": data["bottom"]["die"], "dbu_per_micron": data["bottom"]["units"],
              "candidate_sites_3um": capacity, "headroom_above_one_site_per_cross_die_net": capacity - len(manifest),
              "axis_site_counts_3um": config["site_counts"],
              "prior_terminal_references": len(prior), "minimum_reference_chebyshev_distance_dbu": minimum,
              "closest_reference_net_pair": closest, "reference_pairs_closer_than_3um": close3,
              "reference_pairs_closer_than_2um": close2,
              "source_grid_provenance": "The converter in the supplied package hardcodes pitch=6000 (3 um) as a proposed research grid; it is not read from the placer input",
              "packaged_converter_sha256": hashlib.sha256(packaged_converter).hexdigest(),
              "current_converter_sha256": digest(ROOT / "benchmarks/h3d_aes_convert.py"),
              "upstream_published_default": "100 units = 1 um TerminalSize and 1 um TerminalSpacing; this does not authenticate the missing original placer input",
              "upstream_commit": UPSTREAM_SHA, "upstream_references": refs,
              "missing_original_inputs": "Original placer .input/.txt and pre-conversion DEFs are not in the supplied archive",
              "io_scope": "187 cross-die nets contain IO and lack supplied prior terminals; see separate counters for port-convention-only and internal-cell crossing. Upstream input conversion omits IO-containing nets.",
              "placement_audit": audit["dies"],
              "conclusion_scope": "Internal checks cannot establish detailed routability or authenticate the missing original placement files"}
    (out / "data_provenance_audit.json").write_text(json.dumps(report, indent=2) + "\n")
    # TABLE IV of H3D explicitly evaluates 2 um pitch. Preserve all other fields
    # and the original 3 um configuration; this is a control, not a 3 um repair.
    dense = dict(config)
    dense["pitch_dbu"] = [4000, 4000]
    dense["site_counts"] = [(data["bottom"]["die"][axis + 2] - (config["pad_size_dbu"][axis] - config["pad_size_dbu"][axis] // 2) - config["origin_dbu"][axis]) // 4000 + 1 for axis in (0, 1)]
    dense["_experiment_scope"] = "EXPLICIT_2UM_PITCH_CONTROL_H3D_TABLE_IV_NOT_ORIGINAL_3UM_RESULT"
    (out / "derived/hbt_2um_control.json").write_text(json.dumps(dense, indent=2) + "\n")
    print(json.dumps({key: report[key] for key in ("input_internal_consistency", "cross_die_nets", "candidate_sites_3um",
          "headroom_above_one_site_per_cross_die_net", "minimum_reference_chebyshev_distance_dbu", "reference_pairs_closer_than_3um",
          "reference_pairs_closer_than_2um", "source_grid_provenance")}, indent=2))


if __name__ == "__main__":
    main()
