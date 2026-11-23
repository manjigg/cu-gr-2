"""Run with python3 tests/h3d/test_hbt_count.py /path/to/route."""

import csv
import json
import re
import subprocess
import sys
import tempfile
from pathlib import Path


def main():
    binary = Path(sys.argv[1]).resolve()
    repo = Path(__file__).resolve().parents[2]
    aes_def = repo / "benchmarks/aes/aes_top_hbt.place.def"
    source = aes_def.read_text()
    macro = re.search(r"^    - _16207_ (\S+)", source, re.M).group(1)
    header = source.split("COMPONENTS ", 1)[0]

    with tempfile.TemporaryDirectory() as tmp:
        work = Path(tmp)
        for table in ("POWV9.dat", "POST9.dat"):
            (work / table).symlink_to(repo / "flute" / table)
        config = {
            "package_dbu": 2000,
            "connections": [{"top_net": "signal", "bottom_net": "signal"}],
            "hbt": {"origin": [0, 0], "pitch": [6000, 6000]},
            "hbt_output": "hbt.tsv",
        }
        for side in ("top", "bottom"):
            config[side] = {
                "lef": str(repo / "benchmarks/NangateOpenCellLibraryWithMacro.lef"),
                "def": f"{side}.def",
                "guide": f"{side}.guide",
                "layer": "metal7",
            }
        for clusters, cost, count in ((2, 4, 2), (3, 4, 3), (3, 10000, 1)):
            xs = (30000, 270000) if clusters == 2 else (30000, 150000, 270000)
            placements = "".join(
                f"    - cell{i} {macro} + PLACED ( {x} 150000 ) N ;\n"
                for i, x in enumerate(xs)
            )
            pins = " ".join(f"( cell{i} ZN )" for i in range(clusters))
            placed = (f"COMPONENTS {clusters} ;\n{placements}END COMPONENTS\n"
                      f"PINS 0 ;\nEND PINS\nNETS 1 ;\n    - signal {pins} + USE SIGNAL ;\n"
                      "END NETS\nEND DESIGN\n")
            for side in ("top", "bottom"):
                (work / f"{side}.def").write_text(header + placed)
            config["hbt"]["cost"] = cost
            (work / "config.json").write_text(json.dumps(config))
            run = subprocess.run([str(binary), "-f2f", "config.json"], cwd=work,
                                 capture_output=True, text=True)
            assert run.returncode == 0, run.stdout + run.stderr
            with (work / "hbt.tsv").open() as output:
                rows = list(csv.DictReader(output, delimiter="\t"))
            assert len(rows) == len({row["site_id"] for row in rows}) == count, rows
            assert {row["net_id"] for row in rows} == {"0"}, rows
            for side in ("top", "bottom"):
                guide = (work / f"{side}.guide").read_text().splitlines()
                assert guide.count("signal") == 1, guide
                boxes = [line.split() for line in guide[2:-1]]
                for row in rows:
                    x = int(row[f"{side}_local_x_package_dbu"])
                    y = int(row[f"{side}_local_y_package_dbu"])
                    assert any(
                        layer == row[f"{side}_layer"]
                        and int(xl) <= x <= int(xh)
                        and int(yl) <= y <= int(yh)
                        for xl, yl, xh, yh, layer in boxes
                    ), (side, row)


if __name__ == "__main__":
    main()
