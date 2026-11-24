#!/usr/bin/env python3
"""Replay F2F benchmarks with the same inputs/resource policy in either mode."""
import argparse
import csv
import gzip
import json
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]
CASES = ("aes", "tinyRocket", "ethmac", "jpegencode", "mor1kx", "ariane", "bp")
MACRO_LEF = {"aes", "tinyRocket", "ariane", "bp"}
FIELDS = ("benchmark", "mode", "wirelength_um", "via_count", "used_hbt", "wire_overflow",
          "max_wire_overflow", "unrouted_nets", "hbt_conflicts", "router_reported_seconds",
          "wall_seconds", "max_rss_kb", "exit_code")


def run(binary, out, mode, cases, cost, disable):
    out.mkdir(parents=True, exist_ok=True)
    with (out / "summary.tsv").open("w") as summary:
        writer = csv.DictWriter(summary, FIELDS, delimiter="\t", lineterminator="\n")
        writer.writeheader()
        for case in cases:
            work = out / case
            work.mkdir(exist_ok=True)
            lef = ROOT / "benchmarks" / ("NangateOpenCellLibraryWithMacro.lef" if case in MACRO_LEF
                                          else "NangateOpenCellLibraryWithoutMacro.lef")
            config = {
                "package_dbu": 2000, "paired_ports": True, "hbt_output": "hbt.tsv",
                "candidate_mode": mode, "diagnostics": "diagnostics.jsonl",
                "candidate_search": {key: False for key in disable},
                "hbt": {"origin": [3000, 3000], "pitch": [6000, 6000], "min_spacing": 6000, "cost": cost},
            }
            for side, suffix in (("top", "top"), ("bottom", "btm")):
                config[side] = {
                    "lef": str(lef), "def": str(ROOT / "benchmarks" / case / f"{case}_{suffix}_hbt.place.def"),
                    "layer": "metal7", "guide": f"{side}.guide",
                }
            for table in ("POWV9.dat", "POST9.dat"):
                path = work / table
                if not path.exists():
                    path.symlink_to(ROOT / "flute" / table)
            (work / "f2f.json").write_text(json.dumps(config, indent=2) + "\n")
            cmd = [str(binary), "-f2f", "f2f.json", "-threads", "1"]
            (work / "command.json").write_text(json.dumps(cmd) + "\n")
            with (work / "router.log").open("w") as log:
                proc = subprocess.run(["/usr/bin/time", "-f", "%e\t%M", "-o", "time.tsv", *cmd],
                                      cwd=work, stdout=log, stderr=subprocess.STDOUT)
            wall, rss = (work / "time.tsv").read_text().splitlines()[-1].split("\t")
            result = dict(benchmark=case, mode=mode, wall_seconds=wall, max_rss_kb=rss,
                          exit_code=proc.returncode)
            for line in (work / "router.log").read_text().splitlines():
                if "F2F_METRICS " in line:
                    metrics = json.loads(line.split("F2F_METRICS ", 1)[1])
                    result.update({k: v for k, v in metrics.items() if k in FIELDS})
            source = work / "diagnostics.jsonl"
            last_stage = None
            if source.exists():
                # Keep failure traces as well, even when the router could not finalize a stage.
                with source.open() as stream:
                    for line in stream:
                        if line.startswith('{"dies":') and '"type":"stage"' in line:
                            last_stage = json.loads(line)
                with source.open("rb") as src, gzip.open(work / "diagnostics.jsonl.gz", "wb", compresslevel=1) as dst:
                    shutil.copyfileobj(src, dst)
                source.unlink()
            if proc.returncode == 0:
                if last_stage is None or "used_hbt" not in result:
                    raise RuntimeError(f"{case}: missing router or stage metrics")
                result["max_wire_overflow"] = max(d["max_wire_overflow"] for d in last_stage["dies"])
                result["unrouted_nets"] = 0
                with (work / "hbt.tsv").open() as bonds:
                    sites = [row["site_id"] for row in csv.DictReader(bonds, delimiter="\t")]
                result["hbt_conflicts"] = len(sites) - len(set(sites))
                if len(sites) != result["used_hbt"]:
                    raise RuntimeError(f"{case}: TSV HBT count differs from metrics")
            writer.writerow(result)
            summary.flush()
            print(result, flush=True)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binary", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--mode", choices=("legacy", "improved"), default="improved")
    parser.add_argument("--cases", nargs="+", choices=CASES, default=CASES)
    parser.add_argument("--cost", type=float, default=4.0)
    parser.add_argument("--disable", nargs="*", default=(),
                        choices=("budget_dp", "position_beam", "compare_old", "deduplicate_steps"))
    args = parser.parse_args()
    run(args.binary.resolve(), args.output.resolve(), args.mode, args.cases, args.cost, args.disable)
