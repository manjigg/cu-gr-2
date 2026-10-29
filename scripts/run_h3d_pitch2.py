#!/usr/bin/env python3
"""Run the nominal-2um author-style experiment without overwriting 3um data."""
import argparse
import csv
from datetime import datetime, timezone
import json
import math
from pathlib import Path
import re
import shutil
import subprocess

import h3d_author_benchmarks as driver
from validate_h3d_author import output_check, trace_check

ROOT = driver.ROOT
BASE = ROOT / 'eval/h3d_author/run_002'
EXP = ROOT / 'eval/h3d_author_2um/run_001'
RESULTS = ROOT / 'results-2'


def require(condition, message):
    if not condition:
        raise ValueError(message)


def baseline_files():
    paths = [ROOT / 'detail-routing.csv']
    paths += [p for p in (ROOT / 'results').rglob('*') if p.is_file()]
    paths += [p for p in (ROOT / 'detail-routing').rglob('*') if p.is_file()]
    return {str(p): driver.sha(p) for p in paths}


def check_baseline():
    expected = json.loads((EXP / 'baseline_3um_manifest.json').read_text())
    require(all(Path(p).is_file() and driver.sha(p) == h for p, h in expected.items()), 'Existing 3um results changed')


def prepare():
    require(not EXP.exists() and not RESULTS.exists(), '2um experiment/results already exist')
    driver.check_protected(BASE)
    EXP.mkdir(parents=True)
    for name in ('inputs', 'binaries'):
        (EXP / name).mkdir()
    driver.write_json(EXP / 'baseline_3um_manifest.json', baseline_files())
    driver.write_json(EXP / 'original_manifest.json', driver.protected())
    shutil.copy2(ROOT / 'build/route', EXP / 'binaries/route')
    shutil.copy2(ROOT / 'build/Testing/Temporary/LastTest.log', EXP / 'ctest.log')
    old_audit = json.loads((BASE / 'input_audit.json').read_text())
    audits = {}
    for case in driver.CASES:
        source = BASE / 'inputs' / case
        target = EXP / 'inputs' / case
        target.mkdir()
        for name in ('top.def', 'bottom.def'):
            shutil.copy2(source / name, target / name)
            require(driver.sha(source / name) == driver.sha(target / name), 'Source endpoint/placement input changed')
        entry = old_audit[case]
        config = dict(entry['config'])
        dbu = entry['dbu']
        pitch = 2 * dbu
        bounds = entry['die_bounds_dbu']
        text = (target / 'top.def').read_text()
        track = re.search(r'TRACKS Y\s+(-?\d+)\s+DO\s+(\d+)\s+STEP\s+(\d+)\s+LAYER metal7', text)
        require(track, 'Missing metal7 tracks')
        origin, count, step = map(int, track.groups())
        xs = list(range(bounds[0] + pitch, bounds[2] - pitch + 1, pitch))
        ys, drift = [], 0
        for desired in range(bounds[1] + pitch, bounds[3] - pitch + 1, pitch):
            aligned = origin + math.ceil((desired + drift - origin) / step) * step
            if aligned > bounds[3] - pitch:
                break
            require(0 <= (aligned - origin) // step < count, 'HBT aligned outside declared tracks')
            drift += aligned - desired - drift
            ys.append(aligned)
        require(len(xs) > 1 and len(ys) > 1 and len(set(b - a for a, b in zip(ys, ys[1:]))) == 1, 'Nonuniform/unusable HBT array')
        config.pop('author_nominal_hbt_pitch_um', None)
        config.update(origin_dbu=[xs[0], ys[0]], pitch_dbu=[xs[1] - xs[0], ys[1] - ys[0]],
                      site_counts=[len(xs), len(ys)], nominal_hbt_pitch_um=2.0,
                      hbt_optimization_coordinates='gcell_centers', allow_coincident_hbt_grid_points=True,
                      subnet_routing_topology='flute', subnet_pin_access='physical_shapes',
                      write_routing_traces=False,
                      author_alignment_note='Nominal 2um using the same full-pitch margins and incremental metal7 Y-track alignment rule as the 3um baseline; physical sites remain distinct when GCell representatives coincide.')
        require(config['pad_size_dbu'] == entry['config']['pad_size_dbu'], 'Pad geometry must remain unchanged')
        updated = dict(entry)
        updated.update(config=config, nominal_hbt_pitch_um=2.0,
                       legal_array_site_count=len(xs) * len(ys),
                       reference_3um_available_hbt=entry['author_available_hbt'],
                       reference_3um_used_hbt=entry['author_used_hbt'])
        updated.pop('author_available_hbt', None)
        updated.pop('author_used_hbt', None)
        driver.write_json(target / 'hbt.json', config)
        driver.write_json(target / 'audit.json', updated)
        audits[case] = updated
        print(json.dumps({'case': case, 'nominal_pitch_um': 2, 'actual_xy_pitch_um': [v / dbu for v in config['pitch_dbu']],
                          'pad_um': [v / dbu for v in config['pad_size_dbu']], 'sites': config['site_counts'], 'capacity': len(xs) * len(ys)}), flush=True)
    driver.write_json(EXP / 'input_audit.json', audits)
    driver.write_json(EXP / 'experiment.json', {'created_at_utc': datetime.now(timezone.utc).isoformat(),
        'nominal_pitch_um': 2, 'baseline_experiment': str(BASE), 'global_results': str(RESULTS),
        'binary_sha256': driver.sha(EXP / 'binaries/route'), 'method': 'paper', 'ties': 'first', 'assignment_trials': 1,
        'seed': 0, 'threads': 1, 'actual_pitch_um': [2.0, 2.4], 'pad_um': [0.4, 0.4],
        'representation_note': 'Distinct physical HBT IDs/owners may share a uniform-grid representative; no physical sites are dropped.'})
    driver.write_json(EXP / 'source_hashes.json', {str(p.relative_to(ROOT)): driver.sha(p)
        for base in ('src', 'rsyn') for p in (ROOT / base).rglob('*') if p.is_file()})


def regression():
    target = EXP / 'regression_3um_aes'
    target.mkdir()
    old = BASE / 'verified_summary/aes'
    audit = json.loads((BASE / 'inputs/aes/audit.json').read_text())
    command = [str(EXP / 'binaries/route'), '-lef', audit['lef'],
               '-top-def', str(BASE / 'inputs/aes/top.def'), '-bottom-def', str(BASE / 'inputs/aes/bottom.def'),
               '-hbt-config', str(old / 'hbt.json'), '-output-dir', str(target / 'output'), '-threads', '1',
               '-h3d-method', 'paper', '-h3d-paper-ties', 'first', '-h3d-assignment-trials', '1', '-h3d-seed', '0']
    driver.write_json(target / 'command.json', {'argv': command})
    with (target / 'run.log').open('x') as stream:
        result = subprocess.run(command, cwd=ROOT / 'flute', stdout=stream, stderr=subprocess.STDOUT, timeout=120)
    require(result.returncode == 0, '3um compatibility regression failed')
    checks = {name: driver.sha(target / 'output' / name) == driver.sha(old / 'output' / name)
              for name in ('top.def', 'bottom.def', 'top.guide', 'bottom.guide')}
    driver.write_json(target / 'verification.json', {'status': 'passed' if all(checks.values()) else 'failed', 'byte_identical': checks})
    require(all(checks.values()), 'Dense-site support changed legacy 3um routing outputs')
    check_baseline()
    print(json.dumps({'regression_3um_aes': 'passed', 'byte_identical': checks}, indent=2))


def global_run():
    driver.run(EXP, 'global', EXP / 'binaries/route', driver.CASES, 'first', 0, 'gcell_centers', 'summary')
    old = {r['case']: r for r in json.loads((BASE / 'verified_summary/summary.json').read_text())}
    rows = json.loads((EXP / 'global/summary.json').read_text())
    for row in rows:
        require(row['status'] == 'completed' and row['return_code'] == 0, 'Global routing failed: ' + row['case'])
        require(row['topological_hbt'] == old[row['case']]['topological_hbt'], 'Quantity-DP topology changed beyond HBT pitch')
        require(row['nets'] == old[row['case']]['nets'] and row['cross_die_nets'] == old[row['case']]['cross_die_nets'], 'Original connectivity changed')
    checked = [output_check(EXP, case, 'global') for case in driver.CASES]
    driver.write_json(EXP / 'verification_global.json', {'status': 'passed', 'cases': checked, 'author_inputs_and_results_unchanged': True})
    driver.readback(EXP, 'native_def_readback', EXP / 'binaries/route', driver.CASES, 'global')
    check_baseline()


def publish():
    require(not RESULTS.exists(), 'Refusing to overwrite results-2')
    verification = json.loads((EXP / 'verification_global.json').read_text())
    require(verification['status'] == 'passed', 'Global verification incomplete')
    RESULTS.mkdir()
    audits = json.loads((EXP / 'input_audit.json').read_text())
    rows = json.loads((EXP / 'global/summary.json').read_text())
    old = {r['case']: r for r in json.loads((BASE / 'verified_summary/summary.json').read_text())}
    manifest = {'published_at_utc': datetime.now(timezone.utc).isoformat(), 'kind': 'global_routing_only',
                'source_experiment': str(EXP), 'nominal_pitch_um': 2.0,
                'actual_pitch_um': [2.0, 2.4], 'binary_sha256': driver.sha(EXP / 'binaries/route'), 'cases': {}}
    simple, comparison = [], []
    for row in rows:
        case = row['case']
        source = EXP / 'global' / case
        target = RESULTS / case
        target.mkdir()
        files = {f'{case}_top_hbt.place.def': source / 'output/top.def', f'{case}_btm_hbt.place.def': source / 'output/bottom.def',
                 f'{case}_top.guide': source / 'output/top.guide', f'{case}_btm.guide': source / 'output/bottom.guide',
                 'h3d_report.json': source / 'output/h3d_report.json', 'input_audit.json': EXP / 'inputs' / case / 'audit.json'}
        files.update({n: source / n for n in ('hbt.json', 'run.log', 'time.log', 'metrics.json', 'command.json')})
        copied = {}
        for name, src in files.items():
            dst = target / name
            shutil.copy2(src, dst)
            require(driver.sha(src) == driver.sha(dst), 'Published global artifact mismatch')
            copied[name] = {'source': str(src), 'sha256': driver.sha(dst), 'bytes': dst.stat().st_size}
        manifest['cases'][case] = {'lef': audits[case]['lef'], 'files': copied}
        simple.append({'benchmark': case, 'nominal_hbt_pitch_um': 2.0, 'actual_x_pitch_um': 2.0, 'actual_y_pitch_um': 2.4,
                       'wire_length_um': row['wirelength_um'], 'wire_length_metric': row['wire_length_metric'],
                       'via_count': row['vias'], 'hbt_count': row['hbt'], 'wire_overflow': row['wire_overflow'],
                       'wall_seconds': row['wall_seconds'], 'status': row['status']})
        baseline = old[case]
        comparison.append({'benchmark': case, 'wire_metric_3um': baseline['wire_length_metric'], 'wire_metric_2um': row['wire_length_metric'],
                           'wire_change_pct': (row['wire_length_metric'] / baseline['wire_length_metric'] - 1) * 100,
                           'hbt_3um': baseline['hbt'], 'hbt_2um': row['hbt'],
                           'overflow_3um': baseline['wire_overflow'], 'overflow_2um': row['wire_overflow']})
    for name, data in (('global_routing_summary.csv', simple), ('comparison_vs_3um.csv', comparison)):
        with (RESULTS / name).open('x', newline='', encoding='utf-8-sig') as stream:
            writer = csv.DictWriter(stream, fieldnames=list(data[0])); writer.writeheader(); writer.writerows(data)
    driver.write_json(RESULTS / 'global_routing_summary.json', rows)
    driver.write_json(RESULTS / 'global_routing_verification.json', verification)
    driver.write_json(RESULTS / 'publish_manifest.json', manifest)
    (RESULTS / 'README.md').write_text('''# 标称 2 µm HBT 全局布线结果

本批与原 3 µm 实验使用相同七例输入、0.4×0.4 µm 焊盘、paper/first/单次回溯/seed 0、1 线程。
沿用作者轨道对齐规则，标称 2 µm 的实际中心距为 X=2.0 µm、Y=2.4 µm，不能写成严格 2×2 µm 阵列。
不同物理 HBT 即使投影到同一 GCell，也保留独立 ID、物理位置和占用计数，不丢弃或合并资源。

每例有配套的 `<case>_top_hbt.place.def` / `<case>_top.guide` 和 `<case>_btm_hbt.place.def` / `<case>_btm.guide`。
LEF：aes、tinyRocket、ariane、bp 用 WithMacro；ethmac、mor1kx、jpegencode 用 WithoutMacro，均位于项目 benchmarks_h3d。

- `global_routing_summary.csv`：本次全局指标，非详细布线后的统计。
- `comparison_vs_3um.csv`：与本项目已完成的 3 µm 结果对照，不冒充作者二进制的 2 µm 结果。
- `global_routing_verification.json`：端点、HBT、Guide 与联合逻辑连接核验。
- `publish_manifest.json`：各文件来源和哈希。

详细布线独立运行，成果放项目 detail-routing-2，最终每例两侧合计指标放根目录 detail-routing-2.csv。
原 results、detail-routing 和 detail-routing.csv 保留。
''')
    check_baseline()
    print(json.dumps({'published': str(RESULTS), 'cases': simple}, indent=2))


def trace_aes():
    driver.run(EXP, 'trace_aes', EXP / 'binaries/route', ['aes'], 'first', 0, 'gcell_centers', 'full')
    result = output_check(EXP, 'aes', 'trace_aes')
    result['algorithm'] = trace_check(EXP, 'aes', 'trace_aes', 400)
    result['outputs_identical_to_summary'] = {name: driver.sha(EXP / 'trace_aes/aes/output' / name) ==
        driver.sha(EXP / 'global/aes/output' / name) for name in ('top.def', 'bottom.def', 'top.guide', 'bottom.guide')}
    require(all(result['outputs_identical_to_summary'].values()), 'Dense-grid trace mode changed outputs')
    driver.write_json(EXP / 'verification_dense_trace_aes.json', result)
    check_baseline()
    print(json.dumps(result, indent=2))


def record():
    check_baseline()
    require(driver.sha(ROOT / 'build/route') == driver.sha(EXP / 'binaries/route'), 'Build differs from the 2um run binary')
    source_hashes = json.loads((EXP / 'source_hashes.json').read_text())
    require(all(driver.sha(ROOT / name) == digest for name, digest in source_hashes.items()), 'Global router source changed after its snapshot')
    paths = [Path(__file__), ROOT / 'scripts/h3d_author_benchmarks.py', ROOT / 'scripts/validate_h3d_author.py',
             ROOT / 'scripts/sync_detail_routing.py', ROOT / 'tests/h3d/HbtOptimizationTest.cpp',
             ROOT / 'tests/h3d/check_end_to_end.py', Path('/home/wanghh/h3d/OpenROAD/h3d_drt_validation/batch.py'),
             Path('/home/wanghh/h3d/OpenROAD/h3d_drt_validation/audit.py')]
    archive = EXP / 'workflow_snapshot'
    archive.mkdir()
    values = {}
    for i, path in enumerate(paths):
        target = archive / (str(i) + '_' + path.name)
        shutil.copy2(path, target)
        values[str(path)] = {'snapshot': str(target), 'sha256': driver.sha(target)}
    driver.write_json(EXP / 'workflow_manifest.json', {'files': values, 'all_3um_results_unchanged': True,
        'global_binary_matches_build': True, 'global_source_hashes_match': True})
    print(json.dumps({'workflow_snapshots': len(paths), 'all_3um_results_unchanged': True, 'global_binary_matches_build': True}, indent=2))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('phase', choices=('prepare', 'regression', 'global', 'publish', 'trace-aes', 'record'))
    args = parser.parse_args()
    {'prepare': prepare, 'regression': regression, 'global': global_run, 'publish': publish, 'trace-aes': trace_aes, 'record': record}[args.phase]()
