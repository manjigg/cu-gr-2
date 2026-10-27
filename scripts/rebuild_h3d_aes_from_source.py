#!/usr/bin/env python3
"""Authenticate the real Place-LoL AES files and rebuild a separate input."""
import argparse
from collections import Counter
import importlib.util
import json
from pathlib import Path
import re

from h3d_aes_experiment import ROOT, SOURCE, digest, prepare


def summarize(out):
    audit = json.loads((out / "original_source_audit.json").read_text())
    experiment = out / "experiment"
    report = json.loads((experiment / "full/h3d_report.json").read_text())
    verified = json.loads((experiment / "full/independent_verification.json").read_text())
    paper = json.loads((experiment / "full/paper_method_verification.json").read_text())
    assert report["status"] == "completed" and report["completed_input_nets"] == verified["processed_nets_checked"] == 14310
    assert report["cross_die_net_count"] == 1679 and verified["original_endpoints_preserved"] == 51663
    assert report["method_profile"] == "paper" and report["paper_traceback_ties"] == "first" and report["paper_assignment_trial_limit"] == 1
    for feature in ("full_domain_distance_transform", "future_site_reservation", "forced_single_site_budget_fallback",
                    "quantity_dp_equivalent_assignment_search", "zero_cost_common_site_tie_break"):
        assert report["method_features"][feature] is False
    config = report["physical_configuration"]
    assert config["pitch_dbu"] == [4000, 4000] and config["pad_size_dbu"] == [2000, 2000] and config["spacing_dbu"] == 2000
    assert all(digest(Path(path)) == value for path, value in audit["raw_source_sha256"].items())
    for die in ("top", "bottom"):
        assert report["routing"][die]["wire_overflow_tracks"] == report["routing"][die]["demand_overflow_tracks"] == 0
        assert report["pad_capacity_sync"][die]["matches_fresh_final_geometry"] and report["pad_capacity_sync"][die]["demand_preserved"]
    repeats = {name: digest(experiment / "full" / name) == digest(experiment / "full_repeat" / name)
               for name in ("top.def", "bottom.def", "top.guide", "bottom.guide", "routing_traces.jsonl")}
    assert all(repeats.values())
    result = {"status": "source_model_input_corrected_paper_full_routing_verified", "original_source_audit": audit,
              "completed_original_nets": 14310, "cross_die_nets": 1679, "original_endpoints_preserved": 51663,
              "physical_configuration": config, "physical_hbt_count": report["physical_hbt_count"],
              "initial_hbt_sites": report["initial_available_hbt_sites"], "remaining_hbt_sites": report["remaining_available_hbt_sites"],
              "method_profile": report["method_profile"], "method_features": report["method_features"],
              "routing": report["routing"], "pad_capacity_sync": report["pad_capacity_sync"], "paper_verification": paper,
              "native_def_readback": json.loads((experiment / "full_readback/process_status.json").read_text()),
              "repeatability_file_sha256_equal": repeats, "raw_source_files_still_unchanged": True,
              "old_3um_input_result_repaired": False,
              "interpretation": "The source terminal model is 1 um size + 1 um spacing; the regular grid is derived at 2 um pitch. This corrects input adaptation, not the old 3 um scenario.",
              "converter_sha256": digest(ROOT / "benchmarks/h3d_aes_convert.py"), "route_binary_sha256": digest(ROOT / "build/route")}
    (out / "summary.json").write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps({key: result[key] for key in ("status", "physical_hbt_count", "initial_hbt_sites", "remaining_hbt_sites", "routing", "method_features")}, indent=2))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-root", type=Path)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--phase", choices=["prepare", "summary"], default="prepare")
    args = parser.parse_args()
    out = args.out.resolve()
    if args.phase == "summary":
        summarize(out)
        return
    if args.source_root is None:
        parser.error("--source-root is required for source authentication and conversion")
    root = args.source_root.resolve()
    paths = [root / relative for relative in (
        "binaries/converted_input/default/aes.input", "binaries/iccad2022/cadb1021/output/default/aes.txt",
        "benchmarks/3D_input/aes/2_2_floorplan_io.def", "binaries/converted_output/default/cadb1021/aes.def",
        "benchmarks/nangate45/lef/NangateOpenCellLibrary.tech.lef", "benchmarks/nangate45/lef/NangateOpenCellLibrary.macro.mod.lef")]
    before = {str(path): digest(path) for path in paths}
    package_before = {path.name: digest(path) for path in SOURCE.iterdir() if path.is_file()}
    out.mkdir(parents=True, exist_ok=False)
    spec = importlib.util.spec_from_file_location("aes_source_converter", ROOT / "benchmarks/h3d_aes_convert.py")
    converter = importlib.util.module_from_spec(spec); spec.loader.exec_module(converter)
    converted = out / "input_from_originals"
    result = converter.run(root, converted, paths[0], None)
    required = ("top.def", "bottom.def", "tech.lef", "cells.lef", "3d_nets.json", "instance_mapping.json")
    comparisons = {name: digest(converted / name) == digest(SOURCE / name) for name in required}
    assert all(comparisons.values()), "Regenerated endpoint/placement/library files differ from the supplied data"
    # Independently check that the placer input's omitted nets and C_i mapping
    # agree with the original DEF, including repeated instance references.
    _, _, original = converter.load_def(paths[2])
    names = [converter.parse_component(item)[0] for item in original["COMPONENTS"]]
    original_nets = [converter.parse_net(item) for item in original["NETS"]]
    text = paths[0].read_text()
    actual = {}
    current = None
    for line in text.splitlines():
        net = re.match(r"Net N(\d+) (\d+)", line)
        pin = re.match(r"Pin C(\d+)/P\d+", line)
        if net:
            current = int(net[1]); actual[current] = {"declared_pins": int(net[2]), "instances": []}
        elif pin:
            assert current is not None
            actual[current]["instances"].append(names[int(pin[1]) - 1])
    expected = {i + 1: endpoints for i, (_, endpoints, _) in enumerate(original_nets)
                if len(endpoints) > 1 and all(instance != "PIN" for instance, _ in endpoints)}
    assert actual.keys() == expected.keys(), "Placer net IDs do not match the source DEF ordering/omission rules"
    for net_id, endpoints in expected.items():
        assert actual[net_id]["declared_pins"] == len(endpoints) == len(actual[net_id]["instances"])
        assert Counter(actual[net_id]["instances"]) == Counter(instance for instance, _ in endpoints)
    input_instances = re.search(r"^NumInstances (\d+)", text, re.M)
    assert input_instances and int(input_instances[1]) == len(names) == 13210
    raw_config = json.loads((converted / "hbt_config.json").read_text())
    audit = {"status": "authenticated_original_source_and_regenerated_input", "source_root": str(root),
             "raw_source_sha256": before, "source_files_unchanged": all(digest(Path(path)) == value for path, value in before.items()),
             "provided_data_files_unchanged": all(digest(SOURCE / name) == value for name, value in package_before.items()),
             "regenerated_files_byte_identical_to_provided": comparisons,
             "original_def_net_count": len(original_nets), "placer_net_count": len(actual),
             "placer_omitted_io_nets": sum(any(instance == "PIN" for instance, _ in endpoints) for _, endpoints, _ in original_nets),
             "placer_omitted_non_io_singleton_nets": sum(len(endpoints) == 1 and endpoints[0][0] != "PIN" for _, endpoints, _ in original_nets),
             "source_terminal_model": raw_config["grid_parameter_provenance"]["placer_terminal_model"],
             "regenerated_hbt_pitch_dbu": raw_config["assumed_hbt_pitch_dbu"], "regenerated_pad_size_dbu": raw_config["pad_size_dbu"],
             "regenerated_spacing_dbu": raw_config["spacing_dbu"], "converted_source_path": str(converted),
             "raw_source_mapping_checks": "All placer net IDs, pin counts, instance-reference multisets and conversion reference placements match",
             "conversion_counts": result}
    assert audit["source_files_unchanged"] and audit["provided_data_files_unchanged"]
    (out / "original_source_audit.json").write_text(json.dumps(audit, indent=2) + "\n")
    prepare(argparse.Namespace(out=out / "experiment", source=converted))
    print(json.dumps({key: audit[key] for key in ("status", "regenerated_files_byte_identical_to_provided", "source_terminal_model",
          "placer_net_count", "regenerated_hbt_pitch_dbu", "regenerated_pad_size_dbu", "regenerated_spacing_dbu")}, indent=2))


if __name__ == "__main__":
    main()
