"""Integration checks for phase-1 bounds, site legality and resource rollback."""
import csv
import json
from pathlib import Path
import re
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
DEF = (ROOT / "benchmarks/aes/aes_top_hbt.place.def").read_text()
HEADER = DEF.split("COMPONENTS ", 1)[0]
MACRO = re.search(r"^    - _16207_ (\S+)", DEF, re.M).group(1)


def layout(nets, header):
    cells, entries = [], []
    for index, positions in enumerate(nets):
        terms = []
        for term, (x, y) in enumerate(positions):
            name = f"p{index}_{term}"
            cells.append(f"  - {name} {MACRO} + PLACED ( {x} {y} ) N ;\n")
            terms.append(f"( {name} ZN )")
        entries.append(f"  - n{index} {' '.join(terms)} + USE SIGNAL ;\n")
    return (header + f"COMPONENTS {len(cells)} ;\n" + "".join(cells) +
            f"END COMPONENTS\nPINS 0 ;\nEND PINS\nNETS {len(entries)} ;\n" +
            "".join(entries) + "END NETS\nEND DESIGN\n")


def run(binary, work, top, bottom, *, fixed=(), hbt=None, transform=None,
        bottom_header=HEADER, error=None, mode="improved", search=None, mapped=True):
    for table in ("POWV9.dat", "POST9.dat"):
        p = work / table
        if not p.exists():
            p.symlink_to(ROOT / "flute" / table)
    config = {
        "package_dbu": 2000, "hbt": hbt or {"origin": [0, 0], "pitch": [6000, 6000]},
        "hbt_output": "hbt.tsv", "diagnostics": "diagnostics.jsonl", "candidate_mode": mode,
        "connections": [{"top_net": f"n{i}", "bottom_net": f"n{i}"} for i in range(len(top))] if mapped else [],
        "candidate_search": search or {},
    }
    for index, point in fixed:
        config["connections"][index]["fixed_site"] = point
    for side, nets, header in (("top", top, HEADER), ("bottom", bottom, bottom_header)):
        (work / (side + ".def")).write_text(layout(nets, header))
        config[side] = {"lef": str(ROOT / "benchmarks/NangateOpenCellLibraryWithMacro.lef"),
                        "def": side + ".def", "guide": side + ".guide", "layer": "metal7"}
    config["bottom"].update(transform or {})
    (work / "f2f.json").write_text(json.dumps(config))
    proc = subprocess.run([str(binary), "-f2f", "f2f.json"], cwd=work, capture_output=True, text=True)
    (work / "router.log").write_text(proc.stdout + proc.stderr)
    events = [json.loads(line) for line in (work / "diagnostics.jsonl").read_text().splitlines()]
    if error:
        assert proc.returncode and error in proc.stderr, proc.stderr
        return [], events
    assert proc.returncode == 0, proc.stderr
    with (work / "hbt.tsv").open() as bonds:
        rows = list(csv.DictReader(bonds, delimiter="\t"))
    assert len({row["site_id"] for row in rows}) == len(rows)
    assert {int(row["net_id"]) for row in rows} == (set(range(len(top))) if mapped else set())
    return rows, events


def check_rollback(events):
    proposals = [e for e in events if e["type"] == "decision" and e["stage"] > 1 and "old" in e]
    assert proposals, "the congestion fixture must exercise rerouting"
    restored = 0
    for event in proposals:
        old, new = event["old"], event["new"]
        def objective(route):
            return tuple(sum(d[key] for d in route["dies"]) for key in
                         ("wire_overflow_delta", "resource_overflow_delta")) + (route["cost"], len(route["sites"]))
        signs = [a - b for a, b in zip(objective(new), objective(old)) if abs(a - b) > 1e-8]
        if event["action"] == "accept":
            assert signs and signs[0] < 0
        else:
            assert event["action"] == "restore_no_gain" and event["sites"] == old["sites"]
            assert not signs or signs[0] >= 0
            restored += 1
    assert restored, "the fixture must verify a rejected route restores old resources"
    # 每阶段已接受方案的长度和 Via 数应与提交后的全图统计一致。
    selected = {}
    for event in events:
        if event["type"] == "decision" and event["action"] == "accept":
            selected[event["net"]] = event["new"]["dies"]
        if event["type"] == "stage":
            for side in range(2):
                expected = event["dies"][side]
                assert abs(sum(plan[side]["wire_um"] for plan in selected.values()) - expected["wire_um"]) < 1e-6
                assert sum(plan[side]["vias"] for plan in selected.values()) == expected["vias"]


def main():
    binary = Path(sys.argv[1]).resolve()
    with tempfile.TemporaryDirectory() as tmp:
        base = Path(tmp)
        def work(case):
            p = base / case
            p.mkdir()
            return p
        simple = [[(30000, 150000)]]
        rows, _ = run(binary, work("two_pin"), simple, simple)
        assert len(rows) == 1
        hbt = {"origin": [3000, 3000], "pitch": [600, 600]}
        both = simple * 2
        fixed = [(0, [3000, 3000]), (1, [3600, 3600])]
        rows, _ = run(binary, work("same_gcell"), both, both, fixed=fixed, hbt=hbt)
        assert all(row["fixed"] == "1" for row in rows)
        assert len({(row["top_gcell_x"], row["top_gcell_y"]) for row in rows}) == 1
        run(binary, work("conflict"), both, both, fixed=[fixed[0], (1, fixed[0][1])],
            hbt=hbt, error="Fixed HBT is illegal or already reserved")
        blocked = dict(hbt, keepouts=[[[0, 0], [304960, 304960]]])
        _, events = run(binary, work("exhausted"), simple, simple, hbt=blocked,
                        error="No feasible route")
        assert any(e["type"] == "search" and e["empty_site_lists"] > 0 and not e["feasible"] for e in events)
        transform = {"sign": [-1, 1], "offset": [304960, 0]}
        shifted = HEADER.replace("GCELLGRID X 0 DO 72 STEP 4200", "GCELLGRID X 0 DO 61 STEP 5000")
        rows, _ = run(binary, work("mirror"), simple, simple, fixed=[(0, [60000, 60000])],
                      transform=transform, bottom_header=shifted)
        assert int(rows[0]["bottom_local_x_package_dbu"]) == 244960
        assert rows[0]["top_gcell_x"] != rows[0]["bottom_gcell_x"]
        stress = [[(30000, 150000), (270000, 150000)]] * 100
        _, events = run(binary, work("congestion"), stress, stress,
                        hbt={"origin": [2100, 2100], "pitch": [4200, 4200], "cost": 1},
                        search={"max_sites": 2})
        check_rollback(events)
    print("F2F candidate legality, exhaustion, mirror, different grids and rollback passed")


if __name__ == "__main__":
    main()
