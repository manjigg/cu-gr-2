#!/usr/bin/env python3
"""Run each synthetic die through the existing router and validate its guides."""

import argparse
import json
from pathlib import Path
import re
import subprocess


ROOT = Path(__file__).resolve().parents[2]
FIXTURE = ROOT / "tests/data/h3d/tiny"


def check_guides(path, expected_names):
    lines = [line.strip() for line in path.read_text().splitlines() if line.strip()]
    names = set()
    index = 0
    rectangles = 0
    while index < len(lines):
        name = lines[index]
        if name in names:
            raise ValueError("Duplicate guide net: " + name)
        names.add(name)
        if index + 1 >= len(lines) or lines[index + 1] != "(":
            raise ValueError("Missing guide opening bracket for " + name)
        index += 2
        count = 0
        while index < len(lines) and lines[index] != ")":
            fields = lines[index].split()
            if len(fields) != 5 or fields[4] not in {"M1", "M2", "M3"}:
                raise ValueError("Invalid guide record: " + lines[index])
            lx, ly, hx, hy = map(int, fields[:4])
            if not (0 <= lx < hx <= 25200 and 0 <= ly < hy <= 25200):
                raise ValueError("Guide outside the die or degenerate: " + lines[index])
            count += 1
            index += 1
        if index == len(lines) or count == 0:
            raise ValueError("Missing closing bracket or empty guide for " + name)
        rectangles += count
        index += 1
    if names != expected_names:
        raise ValueError("Guide net names do not match the fixture: " + str(names))
    return rectangles


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--route", type=Path, default=ROOT / "build/route")
    parser.add_argument("--output-dir", type=Path, default=ROOT / "build/h3d_smoke")
    args = parser.parse_args()
    binary = args.route.resolve()
    output_dir = args.output_dir.resolve()
    if not binary.is_file():
        parser.error("Build route first: " + str(binary))
    runtime = ROOT / "flute"
    for table in ("POWV9.dat", "POST9.dat"):
        if not (runtime / table).is_file():
            parser.error("Missing FLUTE table; run scripts/build.py first: " + table)

    mapping = json.loads((FIXTURE / "nets.json").read_text())
    hbt = json.loads((FIXTURE / "hbt.json").read_text())
    sites = hbt["site_counts"][0] * hbt["site_counts"][1] - len(hbt["blocked_sites"])
    output_dir.mkdir(parents=True, exist_ok=True)
    for die in ("top", "bottom"):
        names = set(mapping["cross_die_nets"]) | set(mapping["local_nets"][die])
        guide = output_dir / (die + ".guide")
        # A failed run must not be validated using a stale guide from a prior run.
        if guide.exists():
            guide.unlink()
        command = [str(binary), "-lef", str(FIXTURE / "technology.lef"),
                   "-def", str(FIXTURE / (die + ".def")), "-output", str(guide)]
        result = subprocess.run(command, cwd=runtime, stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT, text=True)
        log_path = output_dir / (die + ".log")
        log_path.write_text(result.stdout)
        if result.returncode != 0:
            raise RuntimeError("Router failed for {} (exit {}); see {}".format(
                die, result.returncode, log_path))
        expected_stats = {"num of cells": 4, "num of ports": 1, "num of nets": len(names)}
        for label, count in expected_stats.items():
            if not re.search(re.escape(label) + r"\s*:\s*" + str(count) + r"\s*$",
                             result.stdout, re.MULTILINE):
                raise ValueError("Unexpected {} for {}; see {}".format(label, die, log_path))
        if not re.search(r"gcell grid:\s*6 x 6 x 3\s*$", result.stdout, re.MULTILINE):
            raise ValueError("Unexpected routing grid for " + die)
        rectangles = check_guides(guide, names)
        print("{}: 4 cells, {} net fragments, {} valid guide rectangles".format(
            die, len(names), rectangles))
    print("Fixture passed; 3 cross-die nets, {} available HBT sites (not routed yet).".format(sites))
    print("Outputs: " + str(output_dir))


if __name__ == "__main__":
    main()
