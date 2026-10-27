#!/usr/bin/env python3
"""Read-only AES source audit and reproducible, separately stored input adaptation."""
import argparse
from collections import Counter, defaultdict
from datetime import datetime, timezone
from decimal import Decimal
import hashlib
import json
from pathlib import Path
import re
import subprocess
import time

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "benchmarks/h3d_aes"


def experiment_source(experiment):
    audit = Path(experiment) / "input_audit.json"
    return Path(json.loads(audit.read_text())["source_directory"]) if audit.exists() else SOURCE


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def section(text, name, optional=False):
    match = re.search(r"^\s*" + name + r"\s+(\d+)\s*;(.*?)\bEND\s+" + name + r"\b", text, re.M | re.S)
    if not match:
        if optional:
            return {}
        raise ValueError("Missing DEF section " + name)
    rows = re.findall(r"-\s+(\S+)\s+(.*?);", match[2], re.S)
    if len(rows) != int(match[1]) or len(dict(rows)) != len(rows):
        raise ValueError("Count/duplicate mismatch in " + name)
    return dict(rows)


def def_data(path):
    text = path.read_text()
    units = int(re.search(r"UNITS DISTANCE MICRONS\s+(\d+)", text)[1])
    die = list(map(int, re.search(r"DIEAREA\s*\(\s*(-?\d+)\s+(-?\d+)\s*\)\s*\(\s*(-?\d+)\s+(-?\d+)\s*\)", text).groups()))
    components = {}
    for name, record in section(text, "COMPONENTS").items():
        placed = re.search(r"\+\s*(?:PLACED|FIXED|COVER)\s*\(\s*(-?\d+)\s+(-?\d+)\s*\)\s*(\S+)", record)
        if placed is None:
            raise ValueError("Unplaced component: " + name)
        components[name] = {"macro": record.split()[0], "x": int(placed[1]), "y": int(placed[2]), "orientation": placed[3]}
    nets = {name: re.findall(r"\(\s*(\S+)\s+(\S+)\s*\)", record)
            for name, record in section(text, "NETS").items()}
    return {"text": text, "units": units, "die": die, "components": components,
            "ports": section(text, "PINS", optional=True), "nets": nets}


def macros(text, dbu):
    starts = list(re.finditer(r"^MACRO\s+(\S+)", text, re.M))
    result = {}
    for index, start in enumerate(starts):
        body = text[start.end():starts[index + 1].start() if index + 1 < len(starts) else len(text)]
        size = re.search(r"\bSIZE\s+(\S+)\s+BY\s+(\S+)\s*;", body)
        result[start[1]] = {"size": [int(Decimal(value) * dbu) for value in size.groups()],
                            "pins": set(re.findall(r"^\s*PIN\s+(\S+)", body, re.M))}
    return result


def geometry_audit(data, library):
    buckets, boxes = defaultdict(list), {}
    pairs = set()
    overlaps, examples, outside = 0, [], []
    for name, component in data["components"].items():
        width, height = library[component["macro"]]["size"]
        if component["orientation"] in {"W", "E", "FW", "FE"}:
            width, height = height, width
        x, y = component["x"], component["y"]
        box = (x, y, x + width, y + height)
        boxes[name] = box
        if not (data["die"][0] <= x and data["die"][1] <= y and box[2] <= data["die"][2] and box[3] <= data["die"][3]):
            outside.append(name)
        for bx in range(x // 10000, (box[2] - 1) // 10000 + 1):
            for by in range(y // 10000, (box[3] - 1) // 10000 + 1):
                for other in buckets[(bx, by)]:
                    key = tuple(sorted((name, other)))
                    if key in pairs:
                        continue
                    pairs.add(key)
                    b = boxes[other]
                    area = max(0, min(box[2], b[2]) - max(box[0], b[0])) * max(0, min(box[3], b[3]) - max(box[1], b[1]))
                    if area:
                        overlaps += 1
                        if len(examples) < 12:
                            examples.append({"instances": key, "overlap_area_dbu2": area})
                buckets[(bx, by)].append(name)
    return {"x_site_aligned": sum(c["x"] % 380 == 0 for c in data["components"].values()),
            "y_row_aligned": sum(c["y"] % 2800 == 0 for c in data["components"].values()),
            "bounding_boxes_outside_die": len(outside), "outside_examples": outside[:12],
            "positive_overlap_pairs": overlaps, "overlap_examples": examples,
            "orientation_counts": dict(Counter(c["orientation"] for c in data["components"].values()))}


def center_site_counts(diearea, origin, pitch, pad=None):
    """Count centers with room for a positive pad, excluding the upper boundary."""
    if len(origin) != 2 or len(pitch) != 2 or any(value <= 0 for value in pitch):
        raise ValueError("Expected two positive grid pitches and two origin coordinates")
    upper = [max(1, value - value // 2) for value in pad] if pad is not None else [1, 1]
    counts = [max(0, (diearea[axis + 2] - upper[axis] - origin[axis]) // pitch[axis] + 1) for axis in (0, 1)]
    if not all(counts):
        raise ValueError("HBT grid has no interior candidate centers")
    return counts


def prepare(args):
    source_root = args.source.resolve()
    target = args.out.resolve()
    target.mkdir(parents=True, exist_ok=False)
    derived = target / "derived"
    derived.mkdir()
    hashes = {path.name: digest(path) for path in sorted(source_root.iterdir()) if path.is_file()}
    (target / "source_sha256.json").write_text(json.dumps(hashes, indent=2) + "\n")
    core_files = ["src/h3d/DieAssignment.cpp", "src/h3d/HbtLocationOptimizer.cpp", "src/h3d/HbtGrid.cpp",
                  "src/h3d/SubnetDecomposer.cpp", "src/gr/PatternRoute.cpp", "src/gr/MazeRoute.cpp"]
    (target / "core_source_sha256.json").write_text(json.dumps({name: digest(ROOT / name) for name in core_files}, indent=2) + "\n")
    data = {die: def_data(source_root / (die + ".def")) for die in ("top", "bottom")}
    if data["top"]["units"] != data["bottom"]["units"] or data["top"]["die"] != data["bottom"]["die"]:
        raise ValueError("Inconsistent die units/bounds")
    dbu = data["top"]["units"]
    library = macros((source_root / "cells.lef").read_text(), dbu)
    global_nets, seen_pins = defaultdict(list), set()
    for die, contents in data.items():
        for name, endpoints in contents["nets"].items():
            if not endpoints:
                raise ValueError("Empty source net: " + name)
            for instance, pin in endpoints:
                key = (die, instance, pin)
                if key in seen_pins:
                    raise ValueError("A physical endpoint occurs on multiple source nets: " + str(key))
                seen_pins.add(key)
                if instance == "PIN":
                    if pin not in contents["ports"]:
                        raise ValueError("Missing port " + pin)
                else:
                    macro = contents["components"][instance]["macro"]
                    if pin not in library[macro]["pins"]:
                        raise ValueError("Missing LEF pin: " + str(key))
                global_nets[name].append(key)
    crossing = {name: endpoints for name, endpoints in global_nets.items() if len({p[0] for p in endpoints}) == 2}
    manifest = json.loads((source_root / "3d_nets.json").read_text())
    manifest_by_name = {net["original_net_name"]: net for net in manifest["nets"]}
    if len(manifest_by_name) != len(manifest["nets"]) or set(manifest_by_name) != set(crossing):
        raise ValueError("Cross-die manifest net names differ from DEF union")
    for name, endpoints in crossing.items():
        record = manifest_by_name[name]
        expected = [(die, pin["instance"], pin["pin"]) for die in ("top", "bottom") for pin in record[die + "_pins"]]
        if Counter(expected) != Counter(endpoints):
            raise ValueError("Manifest endpoint mismatch: " + name)
    mapping = json.loads((source_root / "instance_mapping.json").read_text())
    expected_instances = {(die, name, component["macro"]) for die, contents in data.items()
                          for name, component in contents["components"].items()}
    if {(item["die"], item["name"], item["macro"]) for item in mapping.values()} != expected_instances:
        raise ValueError("instance_mapping differs from actual components")

    # 不裁切/重写原工艺或单元几何，只在派生文件中合并两套 LEF 的头部和 END LIBRARY。
    tech = re.sub(r"^\s*END LIBRARY\s*$", "", (source_root / "tech.lef").read_text(), flags=re.M)
    cells = re.sub(r"^\s*(?:VERSION|BUSBITCHARS|DIVIDERCHAR)\s+[^\n]*$", "", (source_root / "cells.lef").read_text(), flags=re.M)
    cells = re.sub(r"^\s*END LIBRARY\s*$", "", cells, flags=re.M)
    (derived / "combined.lef").write_text(tech + "\n" + cells + "\nEND LIBRARY\n")
    proposed = json.loads((source_root / "hbt_config.json").read_text())
    origin = proposed["grid_origin_dbu"]
    pitch = proposed["assumed_hbt_pitch_dbu"]
    diearea = data["top"]["die"]
    native = {"schema_version": 1, "dbu_per_micron": dbu, "coordinate_frame": "bonded_xy",
              "top_transform": "identity", "bottom_transform": "identity", "origin_dbu": origin,
              "pitch_dbu": [pitch, pitch],
              "site_counts": center_site_counts(diearea, origin, [pitch, pitch], proposed.get("pad_size_dbu")),
              "pad_size_dbu": proposed.get("pad_size_dbu"), "blocked_sites": [],
              "landing_layers": {die: proposed["hbt_interface_layer"] for die in ("top", "bottom")},
              "routing_layers": proposed["routing_layers_per_die"],
              "gcell_pitch_dbu": proposed["assumed_gcell_pitch_dbu"],
              "_physical_status": "proposed grid; pad dimensions absent unless explicitly supplied in source",
              "_reference_terminals_used_as_constraints": False,
               "_site_count_definition": "candidate centers inside die; not a pad-legality assertion"}
    native["_source_grid_provenance"] = proposed.get("grid_parameter_provenance", {"kind": "legacy_proposed_research_grid_no_placer_spec"})
    native["spacing_dbu"] = proposed.get("spacing_dbu", 0)
    if native["pad_size_dbu"] is not None:
        native["_physical_status"] = proposed.get("pad_geometry_source", "supplied terminal-model geometry; not physical DRC certification")
    (derived / "hbt_native.json").write_text(json.dumps(native, indent=2) + "\n")
    simple = sorted((net for net in manifest["nets"] if not net["contains_external_io"]),
                    key=lambda net: (len(net["top_pins"]) + len(net["bottom_pins"]), net["source_net_index"]))
    two = [net for net in simple if len(net["top_pins"]) + len(net["bottom_pins"]) == 2][:4]
    multiple = [net for net in simple if 3 <= len(net["top_pins"]) + len(net["bottom_pins"]) <= 6][:4]
    io = sorted((net for net in manifest["nets"] if net["contains_external_io"]),
                key=lambda net: len(net["top_pins"]) + len(net["bottom_pins"]))[:2]
    selected = [net["original_net_name"] for net in two + multiple + io]
    (derived / "selected_nets.json").write_text(json.dumps(selected, indent=2) + "\n")
    audit = {"created_utc": datetime.now(timezone.utc).isoformat(), "source_directory": str(source_root),
             "units_dbu_per_micron": dbu, "diearea_dbu": diearea,
             "original_net_count": len(global_nets), "cross_die_net_count": len(crossing),
             "original_endpoint_count": len(seen_pins), "manifest_endpoint_check": "exact multiset match",
             "instance_mapping_check": "pass", "selected_net_names": selected,
             "cross_die_with_external_io": sum(net["contains_external_io"] for net in manifest["nets"]),
             "cross_die_with_io_assumption": sum(net["contains_external_io"] and
                 all(pin["instance"] == "PIN" for pin in net["bottom_pins"]) for net in manifest["nets"]),
             "old_terminal_reference_count": sum(net.get("prior_terminal_reference_dbu") is not None for net in manifest["nets"]),
             "old_terminals_are_constraints": False, "pad_geometry_missing": native["pad_size_dbu"] is None,
             "proposed_center_site_count": native["site_counts"][0] * native["site_counts"][1],
             "physical_drc_evaluated": False, "dies": {}}
    for die, contents in data.items():
        audit["dies"][die] = {"component_count": len(contents["components"]), "port_count": len(contents["ports"]),
                                "local_net_count": len(contents["nets"]),
                                "endpoint_count": sum(map(len, contents["nets"].values())),
                                **geometry_audit(contents, library)}
    (target / "input_audit.json").write_text(json.dumps(audit, indent=2) + "\n")
    print(json.dumps(audit, indent=2))
    print("Prepared experiment: " + str(target))


def execute(args):
    experiment = args.experiment.resolve()
    source_root = experiment_source(experiment)
    stage = experiment / (args.label or args.stage)
    stage.mkdir(exist_ok=False)
    command = [str((ROOT / "build/route").resolve()), "-lef", str(experiment / "derived/combined.lef"),
               "-top-def", str(source_root / "top.def"), "-bottom-def", str(source_root / "bottom.def"),
               "-hbt-config", str(args.config.resolve() if args.config else experiment / "derived/hbt_native.json"), "-3d-nets", str(source_root / "3d_nets.json"),
               "-output-dir", str(stage)]
    command.extend(["-h3d-method", args.method, "-h3d-seed", str(args.seed)])
    command.extend(["-h3d-paper-ties", args.paper_ties])
    command.extend(["-h3d-assignment-trials", str(args.assignment_trials)])
    if args.stage == "initialization":
        command.append("-h3d-init-only")
    elif args.stage in {"small_topology", "all_topology"}:
        command.append("-h3d-topology-only")
        if args.stage == "small_topology":
            command.extend(["-net-selection", str(experiment / "derived/selected_nets.json")])
    elif args.stage == "small_routing":
        command.extend(["-net-selection", str(args.selection.resolve() if args.selection else experiment / "derived/selected_nets.json")])
    (stage / "command.json").write_text(json.dumps({"argv": command, "cwd": str(ROOT / "flute")}, indent=2) + "\n")
    started = time.monotonic()
    with (stage / "run.log").open("w") as log:
        try:
            result = subprocess.run(command, cwd=ROOT / "flute", stdout=log, stderr=subprocess.STDOUT,
                                    timeout=args.timeout)
            status = {"exit_code": result.returncode, "timed_out": False}
        except subprocess.TimeoutExpired:
            status = {"exit_code": None, "timed_out": True}
    status["wall_seconds"] = time.monotonic() - started
    expected = json.loads((experiment / "source_sha256.json").read_text())
    status["original_data_sha256_unchanged"] = all(digest(source_root / name) == value for name, value in expected.items())
    (stage / "process_status.json").write_text(json.dumps(status, indent=2) + "\n")
    print(json.dumps(status, indent=2))
    print("Log: " + str(stage / "run.log"))
    if status["timed_out"]:
        raise SystemExit(124)
    if status["exit_code"]:
        code = status["exit_code"]
        raise SystemExit(code if code > 0 else 128 - code)


def authorize_pad(args):
    experiment = args.experiment.resolve()
    path = experiment / "derived/hbt_native_pad_014um.json"
    if path.exists():
        raise FileExistsError(path)
    config = json.loads((experiment / "derived/hbt_native.json").read_text())
    config["pad_size_dbu"] = [280, 280]
    config["_physical_status"] = "user-authorized experimental 0.14 x 0.14 um pad; not verified HBT process geometry or DRC"
    config["_pad_dimension_authorization"] = "User selected: 允许0.14µm试验pad"
    config["_authorization_recorded_utc"] = datetime.now(timezone.utc).isoformat()
    path.write_text(json.dumps(config, indent=2, ensure_ascii=False) + "\n")
    print(path)


def failure_selections(args):
    experiment = args.experiment.resolve()
    report = json.loads((experiment / args.input_stage / "h3d_failure_report.json").read_text())
    for name, values in (("failed_net_only.json", [report["failed_net"]]),
                         ("completed_prefix.json", [item["name"] for item in report["nets"]])):
        path = experiment / "derived" / name
        if path.exists():
            raise FileExistsError(path)
        path.write_text(json.dumps(values, indent=2) + "\n")
    print("Prepared explicit diagnostic selections; neither is a full AES success experiment.")


def build(args):
    stage = args.experiment.resolve() / (args.label or "build")
    stage.mkdir(exist_ok=False)
    commands = [["cmake", "-S", "src", "-B", "build", "-DCMAKE_BUILD_TYPE=Release", "-DCUGR_BUILD_TESTS=ON"],
                ["cmake", "--build", "build", "--parallel", "2"]]
    statuses = []
    for index, command in enumerate(commands):
        started = time.monotonic()
        with (stage / ("configure.log" if index == 0 else "compile.log")).open("w") as log:
            result = subprocess.run(command, cwd=ROOT, stdout=log, stderr=subprocess.STDOUT, timeout=600)
        statuses.append({"argv": command, "cwd": str(ROOT), "exit_code": result.returncode,
                         "wall_seconds": time.monotonic() - started})
        (stage / "commands_and_status.json").write_text(json.dumps(statuses, indent=2) + "\n")
        if result.returncode:
            raise RuntimeError("Build failed; see " + str(stage))
    print(json.dumps(statuses, indent=2))


def regressions(args):
    experiment = args.experiment.resolve()
    stage = experiment / (args.label or "regressions")
    stage.mkdir(exist_ok=False)
    commands = [
        ("die_assignment", [str(ROOT / "build/h3d_die_assignment_test")], 0),
        ("hbt_optimization", [str(ROOT / "build/h3d_hbt_optimization_test")], 0),
        ("paper_method", [str(ROOT / "build/h3d_paper_method_test")], 0),
        ("single_die_fixture", ["python3", "tests/h3d/check_fixture.py", "--output-dir", str(stage / "single_die")], 0),
        ("end_to_end", ["python3", "tests/h3d/check_end_to_end.py", "--output-dir", str(stage / "end_to_end")], 0)]
    evidence = experiment / "evidence"
    evidence.mkdir(exist_ok=True)
    # 历史反例源码保持不变；用当前核心重新编译到新实验目录，验证旧反例已修复。
    evidence_source = ROOT / "eval/h3d_aes/run_001/evidence"
    core = ["src/h3d/H3DTree.cpp", "src/h3d/DieAssignment.cpp", "src/h3d/HbtGrid.cpp", "src/h3d/HbtLocationOptimizer.cpp"]
    for name, expected in (("location_candidate_pruning_repro", 0), ("sequential_resource_exhaustion_repro", 0)):
        binary = stage / name
        command = ["c++", "-std=c++14", "-O2", "-Wall", "-Wextra", "-Werror", "-Isrc", "-isystem", "rsyn/src",
                   str(evidence_source / (name + ".cpp")), *core, "-o", str(binary)]
        commands.extend([("compile_" + name, command, 0), (name, [str(binary)], expected)])
    statuses = []
    for name, command, expected in commands:
        started = time.monotonic()
        with (stage / (name + ".log")).open("w") as log:
            result = subprocess.run(command, cwd=ROOT, stdout=log, stderr=subprocess.STDOUT, timeout=180)
        status = {"name": name, "argv": command, "cwd": str(ROOT), "exit_code": result.returncode,
                  "expected_exit_code": expected, "wall_seconds": time.monotonic() - started}
        statuses.append(status)
        (stage / "commands_and_status.json").write_text(json.dumps(statuses, indent=2) + "\n")
        if result.returncode != expected:
            raise RuntimeError("Unexpected regression/evidence outcome: " + name)
    print(json.dumps(statuses, indent=2))


def sanitizers(args):
    experiment = args.experiment.resolve()
    stage = experiment / (args.label or "sanitizers")
    stage.mkdir(exist_ok=False)
    core = ["src/h3d/H3DTree.cpp", "src/h3d/DieAssignment.cpp", "src/h3d/HbtGrid.cpp", "src/h3d/HbtLocationOptimizer.cpp"]
    binary = stage / "hbt_optimization_asan"
    commands = [["c++", "-std=c++14", "-g", "-Wall", "-Wextra", "-Werror", "-fsanitize=address,undefined",
                 "-fno-omit-frame-pointer", "-Isrc", "-isystem", "rsyn/src", "tests/h3d/HbtOptimizationTest.cpp", *core, "-o", str(binary)],
                [str(binary)]]
    paper_binary = stage / "paper_method_asan"
    commands.extend([["c++", "-std=c++14", "-g", "-Wall", "-Wextra", "-Werror", "-fsanitize=address,undefined",
                      "-fno-omit-frame-pointer", "-Isrc", "-isystem", "rsyn/src", "tests/h3d/PaperMethodTest.cpp", *core,
                      "src/h3d/SubnetPartition.cpp", "-o", str(paper_binary)], [str(paper_binary)]])
    names = ["compile_exact.log", "run_exact.log", "compile_paper.log", "run_paper.log"]
    statuses = []
    for i, command in enumerate(commands):
        with (stage / names[i]).open("w") as log:
            result = subprocess.run(command, cwd=ROOT, stdout=log, stderr=subprocess.STDOUT, timeout=180)
        statuses.append({"argv": command, "exit_code": result.returncode})
        (stage / "commands_and_status.json").write_text(json.dumps(statuses, indent=2) + "\n")
        if result.returncode:
            raise RuntimeError("Sanitizer compilation/test failed; see " + str(stage))
    print("Paper Eq6/Eq7 / splitting and enhanced exact L1 / budget tests passed AddressSanitizer and UBSan.")


def readback(args):
    experiment = args.experiment.resolve()
    source_root = experiment_source(experiment)
    source_stage = experiment / args.input_stage
    stage = experiment / (args.label or "full_readback")
    stage.mkdir(exist_ok=False)
    report = json.loads((source_stage / "h3d_report.json").read_text())
    readback_config = stage / "hbt_config_from_report.json"
    readback_config.write_text(json.dumps(report["physical_configuration"], indent=2) + "\n")
    command = [str(ROOT / "build/route"), "-lef", str(experiment / "derived/combined.lef"),
               "-top-def", str(source_stage / "top.def"), "-bottom-def", str(source_stage / "bottom.def"),
               "-hbt-config", str(readback_config),
               "-output-dir", str(stage), "-h3d-init-only"]
    # 输出已拆分为独立单 die 子网，不能用原始跨 die 网名 manifest 重新绑定。
    (stage / "command.json").write_text(json.dumps({"argv": command, "cwd": str(ROOT / "flute")}, indent=2) + "\n")
    with (stage / "run.log").open("w") as log:
        result = subprocess.run(command, cwd=ROOT / "flute", stdout=log, stderr=subprocess.STDOUT, timeout=180)
    status = {"exit_code": result.returncode, "input_kind": "decomposed_output_defs_initialization_only", "input_stage": args.input_stage}
    if result.returncode == 0:
        statistics = json.loads((stage / "input_statistics.json").read_text())
        audit = json.loads((experiment / "input_audit.json").read_text())
        processed = {net["name"] for net in report["nets"]}
        for die in ("top", "bottom"):
            assert statistics["dies"][die]["cells"] == audit["dies"][die]["component_count"]
            assert statistics["dies"][die]["ports"] == audit["dies"][die]["port_count"] + report["physical_hbt_count"]
            preserved = sum(name not in processed for name in def_data(source_root / (die + ".def"))["nets"])
            assert statistics["dies"][die]["local_nets"] == report["routing"][die]["subnet_count"] + preserved
        expected = audit["original_endpoint_count"] + 2 * report["physical_hbt_count"]
        assert statistics["original_endpoint_count"] == expected
        status["endpoint_count_checked"] = expected
        status["status"] = "passed_native_def_readback"
    (stage / "process_status.json").write_text(json.dumps(status, indent=2) + "\n")
    if result.returncode:
        raise RuntimeError("Full DEF readback failed")
    print(json.dumps(status, indent=2))


def summarize_paper(args):
    experiment = args.experiment.resolve()
    source_root = experiment_source(experiment)
    full = experiment / args.full_stage
    prefix = experiment / args.prefix_stage
    report = json.loads((full / "h3d_failure_report.json").read_text())
    paper_check = json.loads((full / "paper_method_verification.json").read_text())
    prefix_report = json.loads((prefix / "h3d_report.json").read_text())
    prefix_check = json.loads((prefix / "independent_verification.json").read_text())
    prefix_paper = json.loads((prefix / "paper_method_verification.json").read_text())
    topology = json.loads((experiment / "independent_topology_verification.json").read_text())
    audit = json.loads((experiment / "input_audit.json").read_text())
    expected = json.loads((experiment / "source_sha256.json").read_text())
    assert report["method_profile"] == prefix_report["method_profile"] == "paper"
    assert report["status"] == "failed" and prefix_report["status"] == "completed"
    assert paper_check["completed_original_nets_checked"] == prefix_check["processed_nets_checked"] == report["completed_input_nets"]
    assert prefix_paper["completed_trace_replay"]["comparison_stage"] == args.full_stage
    assert prefix_paper["completed_trace_replay"]["identical_algorithm_records"] == report["completed_input_nets"]
    assert all(digest(source_root / name) == value for name, value in expected.items())
    cross = sum(net["cross_die"] for net in report["nets"])
    oracles = prefix_check["full_grid_location_enumerations"]
    snapshot_paths = sorted((ROOT / "src/h3d").glob("*.[ch]*"))
    snapshot_paths += [ROOT / name for name in ("src/global.h", "src/gr/PatternRoute.cpp", "src/gr/MazeRoute.cpp",
                      "scripts/h3d_aes_experiment.py", "scripts/validate_h3d_aes.py", "scripts/validate_h3d_paper.py", "scripts/analyze_h3d_paper_ties.py")]
    summary = {"status": "paper_method_verified_on_completed_prefix_full_aes_exhausts_hbt_sites",
               "method_profile": "paper", "random_seed": report["random_seed"],
               "paper_assignment_trial_limit": report.get("paper_assignment_trial_limit", 1),
               "paper_traceback_ties": report.get("paper_traceback_ties", "first"),
               "original_source_files_sha256_unchanged": True,
               "original_net_count": audit["original_net_count"], "cross_die_net_count": audit["cross_die_net_count"],
               "completed_original_nets": report["completed_input_nets"], "completed_cross_die_nets": cross,
               "unprocessed_original_nets": report["unprocessed_input_nets"],
               "unprocessed_cross_die_nets": audit["cross_die_net_count"] - cross,
               "hbt_sites_used": report["allocated_hbt_sites_including_failed_net"],
               "hbt_site_capacity": report["initial_available_hbt_sites"],
               "failed_net": report["failed_net"], "failed_stage": report["failed_stage"], "error": report["error"],
               "full_design_completed": False, "full_stage": args.full_stage,
               "full_process": json.loads((full / "process_status.json").read_text()),
               "method_features": report["method_features"], "paper_method_checks": paper_check,
               "all_loaded_topology_checks": topology,
               "completed_prefix_output": {"stage": args.prefix_stage,
                   "connectivity_checked_nets": prefix_check["processed_nets_checked"],
                   "original_endpoints_preserved": prefix_check["original_endpoints_preserved"],
                   "unprocessed_original_nets_preserved": prefix_check["unprocessed_original_nets_preserved"],
                   "hbt_conflicts": prefix_check["conflicts"], "routing": prefix_report["routing"],
                   "pad_capacity_sync": prefix_report["pad_capacity_sync"],
                   "identical_initial_algorithm_records": prefix_paper["completed_trace_replay"]["identical_algorithm_records"],
                   "verified_future_net_counter_offset": prefix_paper["completed_trace_replay"]["verified_future_net_counter_offset"],
                   "single_hbt_all_free_site_oracles": len(oracles),
                   "all_free_site_gap_cases": sum(item["gap_dbu"] != 0 for item in oracles),
                   "maximum_all_free_site_gap_dbu": max(item["gap_dbu"] for item in oracles)},
               "sanitizers": json.loads((experiment / args.sanitizer_stage / "commands_and_status.json").read_text()),
               "implementation_choices_not_specified_by_paper": ["mt19937 seed and exact random sequence",
                   "all nearest points when directional L1 distance ties",
                   "zero-wirelength common-site/group traceback tie selection; optional equivalent quantity assignment search",
                   "physical pin access selection and HPWL tie ordering", "error on exhausted HBT grid"],
               "interpretation": "Candidate-set Equation-7 consistency, not full-domain global optimality or author-source identity; site exhaustion is not an infeasibility proof.",
               "current_source_sha256": {str(path.relative_to(ROOT)): digest(path) for path in snapshot_paths if path.is_file()},
               "route_binary_sha256": digest(ROOT / "build/route")}
    capacity_path = experiment / "tie_capacity_analysis.json"
    if capacity_path.exists():
        capacity = json.loads(capacity_path.read_text())
        summary["capacity_analysis"] = {key: value for key, value in capacity.items() if key != "examples"}
    variants = {}
    for name in ("original_first_ties", "position_ties_only", args.full_stage):
        variant_path = experiment / name / "paper_method_verification.json"
        if variant_path.exists():
            checked = json.loads(variant_path.read_text())
            variants[name] = {key: checked[key] for key in ("completed_original_nets_checked", "cross_die_location_problems_checked",
                "zero_cost_coalesced_nets", "sites_saved_by_equal_wirelength_ties_in_this_run", "failed_net")}
    if variants:
        summary["tie_policy_runs"] = variants
    readback_path = experiment / "completed_prefix_readback/process_status.json"
    if readback_path.exists():
        summary["completed_prefix_output"]["native_def_readback"] = json.loads(readback_path.read_text())
    repeat = experiment / "paper_full_repeat"
    if repeat.exists():
        other = json.loads((repeat / "h3d_failure_report.json").read_text())
        assert all(other[key] == report[key] for key in ("failed_net", "failed_stage", "completed_input_nets", "random_seed"))
        assert digest(repeat / "routing_traces.jsonl") == digest(full / "routing_traces.jsonl")
        summary["repeatability"] = {"same_failure_and_completed_counts": True, "routing_trace_sha256_identical": True}
    (experiment / "summary.json").write_text(json.dumps(summary, indent=2) + "\n")
    print(json.dumps({key: summary[key] for key in ("status", "completed_original_nets", "completed_cross_die_nets",
        "unprocessed_original_nets", "hbt_sites_used", "failed_net", "full_design_completed")}, indent=2))


def repair_summary(args):
    experiment = args.experiment.resolve()
    source_root = experiment_source(experiment)
    baseline = ROOT / "eval/h3d_aes/run_001"
    audit = json.loads((experiment / "input_audit.json").read_text())
    report = json.loads((experiment / "full/h3d_report.json").read_text())
    verified = json.loads((experiment / "full/independent_verification.json").read_text())
    assert report["status"] == "completed" and report["completed_input_nets"] == report["selected_net_count"] == audit["original_net_count"]
    assert report["cross_die_net_count"] == audit["cross_die_net_count"]
    assert verified["processed_nets_checked"] == audit["original_net_count"] and verified["conflicts"] == 0
    assert all(row["wire_overflow_tracks"] == row["demand_overflow_tracks"] == 0 for row in report["routing"].values())
    exact = verified["full_grid_location_enumerations"]
    assert all(item["gap_dbu"] == 0 for item in exact)
    replay = json.loads((experiment / "historical_location_replay/result.json").read_text())
    assert replay["status"] == "passed" and replay["case_count"] == 20
    source = json.loads((experiment / "source_sha256.json").read_text())
    old_core = json.loads((baseline / "core_source_sha256.json").read_text())
    source_unchanged = all(digest(source_root / name) == value for name, value in source.items())
    assert source_unchanged
    cross = [net for net in report["nets"] if net["cross_die"]]
    summary = {
        "status": "full_design_global_routing_verified", "original_data_sha256_unchanged": source_unchanged,
        "input": {key: audit[key] for key in ("original_net_count", "cross_die_net_count", "original_endpoint_count", "dies")},
        "processed_original_nets": report["completed_input_nets"], "failed_or_unprocessed_nets": 0,
        "cross_die_nets": report["cross_die_net_count"] , "physical_hbt_count": report["physical_hbt_count"],
        "initial_hbt_capacity": report["initial_available_hbt_sites"], "remaining_hbt_sites": report["remaining_available_hbt_sites"],
        "topological_hbt_count": report["topological_hbt_count"], "hbt_conflicts": verified["conflicts"],
        "future_reserve_checks": verified["future_capacity_budget_checks"],
        "location_modes_cross_die_nets": dict(Counter(net["location_mode"] for net in cross)),
        "multi_hbt_nets_coalesced_by_budget": sum(net["site_budget_fallback"] and net["topological_hbt_count"] > 1 for net in cross),
        "routing": report["routing"], "wirelength_um": sum(row["wirelength_dbu"] for row in report["routing"].values()) / audit["units_dbu_per_micron"],
        "via_count": sum(row["via_count"] for row in report["routing"].values()),
        "pad_capacity_sync": report["pad_capacity_sync"], "phase_seconds": report["phase_seconds"],
        "process_status": json.loads((experiment / "full/process_status.json").read_text()),
        "historical_failed_availability_snapshots_repaired": replay["case_count"],
        "single_hbt_full_site_oracles": len(exact), "single_hbt_oracle_gaps": sum(item["gap_dbu"] != 0 for item in exact),
        "quantity_dp_independent_checks": verified["independent_count_dp_nets"],
        "native_def_readback": json.loads((experiment / "full_readback/process_status.json").read_text()),
        "sanitizers": json.loads((experiment / "sanitizers/commands_and_status.json").read_text()),
        "changed_core_cpp_since_run_001": [name for name, value in old_core.items() if digest(ROOT / name) != value],
        "physical_constraints": {"hbt_pad": "user-authorized experimental 0.14 x 0.14 um", "hbt_pitch_um": 3,
                                 "io_assignment_affected_cross_die_nets": audit["cross_die_with_io_assumption"],
                                 "x_site_misaligned_cells": sum(item["component_count"] - item["x_site_aligned"] for item in audit["dies"].values()),
                                 "detailed_routing_performed": False, "physical_drc_performed": False}}
    repeated = experiment / "full_repeat"
    if repeated.is_dir():
        other = json.loads((repeated / "h3d_report.json").read_text())
        def deterministic(value):
            value = dict(value); value.pop("phase_seconds", None); value.pop("total_seconds", None)
            return value
        assert deterministic(report) == deterministic(other), "Full-design results are not deterministic"
        hashes = {name: digest(experiment / "full" / name) == digest(repeated / name)
                  for name in ("top.def", "bottom.def", "top.guide", "bottom.guide", "routing_traces.jsonl")}
        assert all(hashes.values()), "Full-design files differ on repetition"
        summary["repeatability"] = {"reports_equal_except_timing": True, "file_sha256_equal": hashes,
                                    "process_status": json.loads((repeated / "process_status.json").read_text())}
    (experiment / "summary.json").write_text(json.dumps(summary, indent=2) + "\n")
    print(json.dumps({key: summary[key] for key in ("status", "processed_original_nets", "failed_or_unprocessed_nets", "cross_die_nets",
        "physical_hbt_count", "initial_hbt_capacity", "wirelength_um", "via_count", "single_hbt_full_site_oracles", "single_hbt_oracle_gaps",
        "multi_hbt_nets_coalesced_by_budget")}, indent=2))


def summarize(args):
    experiment = args.experiment.resolve()
    source_root = experiment_source(experiment)
    audit = json.loads((experiment / "input_audit.json").read_text())
    summary = {"input": {key: audit[key] for key in ("original_net_count", "cross_die_net_count", "original_endpoint_count",
                "cross_die_with_io_assumption", "old_terminal_reference_count", "dies")},
               "pad_geometry": "user-authorized experimental 0.14 x 0.14 um; no detailed-routing/DRC claim", "runs": {}}
    for name in ("initialization", "initialization_after_port_fix", "small_topology", "all_topology",
                 "small_routing", "full", "failed_net_isolated", "completed_prefix_refinement"):
        folder = experiment / name
        row = json.loads((folder / "process_status.json").read_text())
        path = folder / "h3d_report.json"
        if not path.exists():
            path = folder / "h3d_failure_report.json"
        if path.exists():
            report = json.loads(path.read_text())
            for key in ("status", "selected_net_count", "completed_input_nets", "failed_net", "failed_stage", "error",
                        "unprocessed_input_nets", "physical_hbt_count", "allocated_hbt_sites_including_failed_net", "initial_available_hbt_sites",
                        "routing", "phase_seconds", "total_seconds", "cross_die_net_count", "topological_hbt_count", "connectivity_checked"):
                if key in report:
                    row[key] = report[key]
            completed_cross = sum(item["cross_die"] for item in report["nets"])
            row["completed_cross_die_nets"] = completed_cross
            row["remaining_cross_die_nets_from_loaded_design"] = audit["cross_die_net_count"] - completed_cross
            row["routing_metrics_scope"] = "completed initial-routing prefix before global refinement" if report["status"] == "failed" else report["experiment_scope"]
        summary["runs"][name] = row
    original = json.loads((experiment / "source_sha256.json").read_text())
    core = json.loads((experiment / "core_source_sha256.json").read_text())
    summary["original_source_files_sha256_unchanged"] = all(digest(source_root / name) == value for name, value in original.items())
    summary["core_algorithm_files_sha256_unchanged"] = all(digest(ROOT / name) == value for name, value in core.items())
    def completed_lines(path):
        with path.open() as file:
            for line in file:
                record = json.loads(line)
                if record.get("status") == "completed":
                    yield record
    import itertools
    summary["prefix_initial_trace_matches_full_completed_prefix"] = all(
        a == b for a, b in itertools.zip_longest(completed_lines(experiment / "full/routing_traces.jsonl"),
                                               completed_lines(experiment / "completed_prefix_refinement/routing_traces.jsonl")))
    pruning = json.loads((experiment / "evidence/candidate_pruning_summary.json").read_text())
    summary["location_candidate_quality"] = {"single_hbt_cases_with_full_site_oracle": pruning["single_movable_hbt_cases_enumerated"],
        "global_optimum_gap_cases": len(pruning["global_optimum_gaps"]),
        "maximum_gap_dbu": max(item["gap_dbu"] for item in pruning["global_optimum_gaps"]),
        "all_gaps_explained_by_candidate_pruning": pruning["all_gaps_explained_by_candidate_pruning"]}
    topology_check = json.loads((experiment / "independent_topology_verification.json").read_text())
    small_check = json.loads((experiment / "small_routing/independent_verification.json").read_text())
    prefix_check = json.loads((experiment / "completed_prefix_refinement/independent_verification.json").read_text())
    summary["algorithm_validation"] = {
        "all_loaded_net_endpoints_match_raw_def": topology_check["all_loaded_net_endpoint_multisets_match_raw_def"],
        "count_dp_independent_reference_nets": topology_check["independent_incident_mask_dp_nets"],
        "count_dp_fully_enumerated_selected_nets": len(topology_check["selected_full_enumeration"]),
        "position_dp_selected_all_site_exact_nets": sum(item["gap_dbu"] == 0 for item in small_check["full_grid_location_enumerations"]),
        "prefix_connected_original_nets": prefix_check["processed_nets_checked"],
        "all_original_endpoints_preserved_in_partial_outputs": prefix_check["original_endpoints_preserved"],
        "hbt_conflicts_in_completed_full_prefix": prefix_check["conflicts"],
        "full_design_routing_completed": False, "detailed_routing_run": False, "physical_drc_run": False}
    summary["unresolved_issues"] = {
        "hbt_ownership_exhaustion": "1764 sites consumed by 1379 cross-die nets; no proof that an alternative allocation is infeasible",
        "candidate_pruning": "20 single-movable-HBT cases differ from the full-site optimum; core transfer matches the restricted candidate optimum",
        "hbt_pad_capacity_update": "GridGraph is built before added HBT pads; their new metal geometry is not dynamically deducted",
        "hbt_pad_geometry": "Only user-authorized 0.14 um experimental geometry, not verified HBT process data",
        "io_assignment": "187 cross-die nets depend on all 388 external IOs being assigned to Bottom",
        "x_site_misaligned_instances": sum(row["component_count"] - row["x_site_aligned"] for row in audit["dies"].values()),
        "f2f_and_drc": "Common projected XY retained; no physical F2F orientation, detailed-routing or DRC certification"}
    summary["regression_and_evidence_commands"] = json.loads((experiment / "regressions/commands_and_status.json").read_text())
    repeated = experiment / "full_repeat_exit_check"
    if repeated.is_dir():
        repeated_report = json.loads((repeated / "h3d_failure_report.json").read_text())
        summary["repeat_full_failure_check"] = {
            "process_status": json.loads((repeated / "process_status.json").read_text()),
            "failed_net": repeated_report["failed_net"], "completed_input_nets": repeated_report["completed_input_nets"],
            "routing_trace_sha256_matches_first_full_run": digest(repeated / "routing_traces.jsonl") == digest(experiment / "full/routing_traces.jsonl")}
    (experiment / "summary.json").write_text(json.dumps(summary, indent=2) + "\n")
    if args.brief:
        print(json.dumps({"summary": str(experiment / "summary.json"),
                          "source_unchanged": summary["original_source_files_sha256_unchanged"],
                          "core_unchanged": summary["core_algorithm_files_sha256_unchanged"],
                          "full_exit_code": summary["runs"]["full"]["exit_code"],
                          "full_completed": summary["runs"]["full"]["completed_input_nets"],
                          "repeat_failure": summary.get("repeat_full_failure_check"),
                          "algorithm_validation": summary["algorithm_validation"]}, indent=2))
    else:
        print(json.dumps(summary, indent=2))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    actions = parser.add_subparsers(dest="action", required=True)
    prepare_parser = actions.add_parser("prepare")
    prepare_parser.add_argument("--out", type=Path, required=True)
    prepare_parser.add_argument("--source", type=Path, default=SOURCE, help="Converted input directory; recorded per experiment")
    prepare_parser.set_defaults(function=prepare)
    build_parser = actions.add_parser("build")
    build_parser.add_argument("--experiment", type=Path, required=True)
    build_parser.add_argument("--label")
    build_parser.set_defaults(function=build)
    pad_parser = actions.add_parser("authorize-pad-014um")
    pad_parser.add_argument("--experiment", type=Path, required=True)
    pad_parser.set_defaults(function=authorize_pad)
    failure_parser = actions.add_parser("failure-selections")
    failure_parser.add_argument("--experiment", type=Path, required=True)
    failure_parser.add_argument("--input-stage", default="full")
    failure_parser.set_defaults(function=failure_selections)
    for name, function in (("regressions", regressions), ("summarize", summarize),
                            ("sanitizers", sanitizers), ("readback", readback), ("summarize-repairs", repair_summary),
                            ("summarize-paper", summarize_paper)):
        command = actions.add_parser(name)
        command.add_argument("--experiment", type=Path, required=True)
        if name == "summarize":
            command.add_argument("--brief", action="store_true")
        elif name in ("regressions", "sanitizers"):
            command.add_argument("--label")
        elif name == "readback":
            command.add_argument("--input-stage", default="full")
            command.add_argument("--label")
        elif name == "summarize-paper":
            command.add_argument("--full-stage", default="full")
            command.add_argument("--prefix-stage", default="completed_prefix_refinement")
            command.add_argument("--sanitizer-stage", default="sanitizers")
        command.set_defaults(function=function)
    run_parser = actions.add_parser("run")
    run_parser.add_argument("--experiment", type=Path, required=True)
    run_parser.add_argument("--stage", choices=["initialization", "small_topology", "all_topology", "small_routing", "full"], required=True)
    run_parser.add_argument("--timeout", type=int, default=300)
    run_parser.add_argument("--label")
    run_parser.add_argument("--config", type=Path)
    run_parser.add_argument("--selection", type=Path)
    run_parser.add_argument("--method", choices=["paper", "enhanced"], default="paper")
    run_parser.add_argument("--seed", type=int, default=0)
    run_parser.add_argument("--paper-ties", choices=["first", "coalesce"], default="coalesce")
    run_parser.add_argument("--assignment-trials", type=int, default=1)
    run_parser.set_defaults(function=execute)
    args = parser.parse_args()
    args.function(args)


if __name__ == "__main__":
    main()
