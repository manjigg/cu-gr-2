"""Summarize new3D evidence without discarding incomplete or failed designs."""
import argparse
import csv
import json
import math
from pathlib import Path
from subprocess import CalledProcessError

from native_bench import CASES
from native_check import check as check_tree
from native_summary import detail_record, global_record, load


GLOBAL_FIELDS = """benchmark status global_nets cross_die_nets real_pins legal_hbt_sites
wirelength_um top_wire_length_um bottom_wire_length_um via_count top_via_count
bottom_via_count used_hbt nets_using_hbt multi_hbt_nets wire_overflow
via_capacity_violations hbt_violations unconnected_nets router_reported_seconds
wall_seconds max_rss_kb best_feasible_objective threads stage2_optimizer_workers stage3_maze_workers independent_check
output_directory reason""".split()
DETAIL_FIELDS = """benchmark status top_wire_length_um bottom_wire_length_um total_wire_length_um
top_via_count bottom_via_count total_via_count hbt_count top_drc bottom_drc total_drc
top_wall_seconds bottom_wall_seconds sum_wall_seconds detail_wall_seconds global_router_seconds
top_geometry bottom_geometry joint_connectivity independent_check output_directory reason""".split()
DATA_ERRORS = (OSError, ValueError, KeyError, IndexError, TypeError, CalledProcessError)


def write_tables(directory, name, fields, rows):
    directory.mkdir(parents=True, exist_ok=True)
    rows = [{k: r.get(k) if r.get(k) is not None else 'N/A' for k in fields} for r in rows]
    for suffix, delimiter in [('csv', ','), ('tsv', '\t')]:
        path = directory / (name + '.' + suffix)
        temp = path.with_suffix('.' + suffix + '.tmp')
        with temp.open('w', newline='') as f:
            writer = csv.DictWriter(f, fields, delimiter=delimiter, lineterminator='\n')
            writer.writeheader()
            writer.writerows(rows)
        temp.replace(path)
    def md(values):
        return '| ' + ' | '.join(str(v).replace('|', '\\|').replace('\n', ' ') for v in values) + ' |'
    content = [md(fields), md(['---'] * len(fields))]
    content += [md(r[k] for k in fields) for r in rows]
    temp = directory / (name + '.md.tmp')
    temp.write_text('\n'.join(content) + '\n')
    temp.replace(directory / (name + '.md'))


def evidence(case, global_dir, detail_dir=None, replay=None, save_tree=False):
    job = (detail_dir or global_dir) / case
    row = dict(benchmark=case, status='BLOCKED', output_directory=str(job))
    try:
        if detail_dir:
            row.update(detail_record(detail_dir, case, global_dir, {}))
            if not (job / 'report.json').is_file():
                row.update(status='BLOCKED', reason='Missing completed detail report')
                return row
            if not (job / 'verified_report.json').is_file() or not (job / 'joint_geometry.json').is_file():
                row.update(status='INCONCLUSIVE' if row['status'] == 'PASSED' else 'FAIL',
                           reason='Missing independent joint conductor evidence')
                return row
            row['independent_check'] = row['joint_connectivity']
            good = row['status'] == 'PASSED' and row['joint_connectivity'] == 'passed'
        else:
            if not (job / 'process.json').is_file():
                row['reason'] = 'Missing process result'
                return row
            row.update(global_record(global_dir, case, replay))
            if row['exit_code'] or not (job / 'ledger-replay.json').is_file():
                row.update(status='FAIL', reason='Global process or ledger replay failed')
                return row
            verified = check_tree(job / 'manifest.json', job / 'output')
            ledger = load(job / 'ledger-replay.json')
            stats = load(job / 'output/routing_statistics.json')
            for stage in stats['stages']:
                if stage['stage'] == 2 and stage.get('local_optimizer'):
                    row['stage2_optimizer_workers'] = stage['local_optimizer'].get('workers')
                if stage['stage'] == 3:
                    row['stage3_maze_workers'] = (stage.get('parallel_maze') or {}).get('actual_workers',1)
            good = (row['unconnected_nets'] == row['via_capacity_violations'] ==
                    row['hbt_violations'] == ledger['resource_keys_after_release'] == 0
                    and max(ledger['max_wire_overflow_by_die']) <= 1e-12)
            row['independent_check'] = 'passed' if good else 'failed'
            if save_tree:
                (job / 'tree-replay.json').write_text(json.dumps(verified, indent=2) + '\n')
        row['status'] = 'PASS' if good else 'FAIL'
        if not good:
            row['reason'] = 'Physical verification failed'
    except DATA_ERRORS as error:
        row['status'] = 'INCONCLUSIVE' if isinstance(error, FileNotFoundError) else 'FAIL'
        row['reason'] = str(error)
    return row


def comparisons(old, new, metrics, runtime):
    result = []
    for a, b in zip(old, new):
        row = dict(benchmark=b['benchmark'], baseline_status=a['status'], new_status=b['status'])
        good = a['status'] == b['status'] == 'PASS'
        for m in metrics:
            x, y = a.get(m), b.get(m)
            row['baseline_' + m], row['new_' + m] = x, y
            if good and x is not None and y is not None:
                row['improvement_pct_' + m] = (x-y) * 100 / x if x else None
                row['delta_' + m] = y-x
            else:
                row['improvement_pct_' + m] = row['delta_' + m] = None
        row['speedup'] = a[runtime]/b[runtime] if good and a.get(runtime) and b.get(runtime) else None
        result.append(row)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('global_dir', type=Path)
    parser.add_argument('output', type=Path, help='Separate summary directory outside the raw run')
    parser.add_argument('--detail-dir', type=Path)
    parser.add_argument('--baseline-global', type=Path)
    parser.add_argument('--baseline-detail', type=Path)
    parser.add_argument('--replay', type=Path)
    a = parser.parse_args()
    if a.baseline_detail and not (a.baseline_global and a.detail_dir):
        parser.error('--baseline-detail requires both --detail-dir and --baseline-global')
    if a.baseline_global and a.detail_dir and not a.baseline_detail:
        parser.error('Provide --baseline-detail for detailed comparisons')
    gd = a.global_dir.resolve()
    dd = a.detail_dir.resolve() if a.detail_dir else None
    replay = a.replay.resolve() if a.replay else None
    if a.output.resolve() in (gd, dd):
        parser.error('Summary directory must differ from raw results')
    rows = [evidence(c, gd, dd, replay, save_tree=True) for c in CASES]
    write_tables(a.output, 'summary', DETAIL_FIELDS if dd else GLOBAL_FIELDS, rows)
    status = dict(passed=sum(r['status'] == 'PASS' for r in rows), designs=len(rows),
                  cases={r['benchmark']:r['status'] for r in rows})
    if a.baseline_global:
        baseline = [evidence(c, a.baseline_global.resolve(), a.baseline_detail.resolve()
                             if a.baseline_detail else None) for c in CASES]
        metrics = ('total_wire_length_um total_via_count hbt_count detail_wall_seconds'.split()
                   if dd else 'wirelength_um via_count used_hbt wall_seconds max_rss_kb'.split())
        compare = comparisons(baseline, rows, metrics, 'detail_wall_seconds' if dd else 'wall_seconds')
        write_tables(a.output, 'comparison', list(compare[0]), compare)
        if all(r['speedup'] for r in compare):
            status['geomean_speedup'] = math.exp(sum(math.log(r['speedup']) for r in compare)/len(compare))
    (a.output/'experiment_status.json').write_text(json.dumps(status, indent=2)+'\n')
    return int(status['passed'] != len(CASES))


if __name__ == '__main__':
    raise SystemExit(main())
