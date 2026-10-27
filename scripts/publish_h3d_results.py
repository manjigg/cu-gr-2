#!/usr/bin/env python3
"""Publish already executed, verified global routes under results/ (no routing)."""
import argparse
import csv
from datetime import datetime, timezone
import json
from pathlib import Path
import re
import shutil

from h3d_author_benchmarks import ROOT, CASES, check_protected, sha, write_json


def require(value, message):
    if not value:
        raise ValueError(message)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--experiment', type=Path, default=ROOT / 'eval/h3d_author/run_002')
    p.add_argument('--destination', type=Path, default=ROOT / 'results')
    args = p.parse_args()
    experiment = args.experiment.resolve()
    destination = args.destination.resolve()
    require(not destination.exists(), 'Refusing to overwrite results: ' + str(destination))
    require(destination.parent.is_dir(), 'Results parent directory does not exist')
    check_protected(experiment)
    summary = json.loads((experiment / 'FINAL_SUMMARY.json').read_text())
    binary_hash = sha(ROOT / 'build/route')
    require(binary_hash == summary['binary_sha256'], 'Current binary differs from verified run')
    sources = json.loads((experiment / 'final_source_hashes.json').read_text())
    require(all(sha(ROOT / name) == digest for name, digest in sources.items()), 'Source differs from verified snapshot')
    verification_path = experiment / 'verification_verified_summary.json'
    verification = json.loads(verification_path.read_text())
    require(verification['status'] == 'passed', 'Independent output verification did not pass')
    verified = {r['case']: r for r in verification['cases']}
    metrics = {r['case']: r for r in json.loads((experiment / 'verified_summary/summary.json').read_text())}
    audit = json.loads((experiment / 'input_audit.json').read_text())
    require(set(metrics) == set(verified) == set(CASES), 'Incomplete seven-case results')
    prepared = {}
    for case in CASES:
        run = experiment / 'verified_summary' / case
        observed = experiment / 'verified_paper' / case / 'output'
        require(metrics[case]['status'] == 'completed' and metrics[case]['return_code'] == 0, 'Run not complete: ' + case)
        require(verified[case]['status'] == 'passed', 'Case verification failed: ' + case)
        require(json.loads((run / 'command.json').read_text())['binary_sha256'] == binary_hash, 'Case binary mismatch')
        pairs = {
            'top.def': f'{case}_top_hbt.place.def',
            'bottom.def': f'{case}_btm_hbt.place.def',
            'top.guide': f'{case}_top.guide',
            'bottom.guide': f'{case}_btm.guide',
        }
        files = {}
        for name, published in pairs.items():
            source = run / 'output' / name
            require(source.stat().st_size > 0 and sha(source) == sha(observed / name), 'Output differs from independently verified run: ' + str(source))
            files[published] = source
        files['h3d_report.json'] = run / 'output/h3d_report.json'
        files.update({name: run / name for name in ('run.log', 'time.log', 'metrics.json', 'command.json', 'hbt.json')})
        files['input_audit.json'] = experiment / 'inputs' / case / 'audit.json'
        prepared[case] = files
    destination.mkdir()
    manifest = {'published_at_utc': datetime.now(timezone.utc).isoformat(),
                'kind': 'global_routing_only', 'source_experiment': str(experiment),
                'publication': 'byte-identical copies of completed verified_summary runs; no router or detailed router invoked',
                'current_binary_sha256': binary_hash, 'cases': {}}
    rows = []
    for case, files in prepared.items():
        target = destination / case
        target.mkdir()
        copied = {}
        for name, source in files.items():
            output = target / name
            shutil.copy2(source, output)
            require(sha(output) == sha(source), 'Published copy mismatch: ' + str(output))
            copied[name] = {'source': str(source), 'sha256': sha(output), 'bytes': output.stat().st_size}
        manifest['cases'][case] = {'lef': audit[case]['lef'], 'files': copied,
                                   'independent_verification': verified[case]}
        m = metrics[case]
        time_text = (target / 'time.log').read_text()
        rss = re.search(r'Maximum resident set size \(kbytes\):\s*(\d+)', time_text)
        rows.append({'benchmark': case, 'threads': 1, 'wire_length_metric': m['wire_length_metric'],
                     'wirelength_um': m['wirelength_um'], 'via_count': m['vias'],
                     'wire_overflow': m['wire_overflow'], 'used_hbt': m['hbt'],
                     'router_reported_seconds': m['total_seconds'], 'wall_seconds': m['wall_seconds'],
                     'max_rss_kb': int(rss[1]) if rss else None, 'exit_code': m['return_code']})
    write_json(destination / 'global_routing_summary.json', rows)
    with (destination / 'global_routing_summary.csv').open('x', newline='') as stream:
        writer = csv.DictWriter(stream, fieldnames=list(rows[0]))
        writer.writeheader()
        writer.writerows(rows)
    shutil.copy2(verification_path, destination / 'global_routing_verification.json')
    write_json(destination / 'publish_manifest.json', manifest)
    lines = ['# 当前代码的七例全局布线结果', '',
             '**七例均已实际运行完成。这里是已验证运行结果的原样发布副本，可作为下一步详细布线的输入。**', '',
             '- 对应当前 `build/route`，SHA-256：`' + binary_hash + '`。',
             '- 原运行及完整证据：`' + str(experiment / 'verified_summary') + '`。',
             '- 发布时核对了当前源码/二进制版本、完整观测模式对应输出，以及全部发布文件的 SHA-256。',
             '- 本目录仅包含全局布线结果；下一步详细布线 DRC 尚未执行。BP 的全局 wire overflow 为 93，其余六例为 0。', '',
             '## 全局指标', '',
             '| Case | 全局线长（µm） | WL metric | 普通 via 数 | HBT 数 | Wire overflow | 耗时（s） |',
             '|---|---:|---:|---:|---:|---:|---:|']
    for r in rows:
        lines.append(f"| {r['benchmark']} | {r['wirelength_um']:.3f} | {r['wire_length_metric']} | {r['via_count']} | {r['used_hbt']} | {r['wire_overflow']:g} | {r['wall_seconds']:.3f} |")
    lines += ['', '线长为全局路由树的 die 内导线长度，按 DBU/2000 换算，不含 HBT 垂直物理长度。WL metric 为归一化指标；二者都不是详细布线后的统计。耗时沿用原实际运行记录，未将复制文件时间计入。', '',
              '## 每例文件与详细布线配套关系', '', '```text',
              '<case>/', '  <case>_top_hbt.place.def   # Top：详细布线读取此 DEF',
              '  <case>_top.guide           # 与上述 Top DEF 配套',
              '  <case>_btm_hbt.place.def   # Bottom：详细布线读取此 DEF',
              '  <case>_btm.guide           # 与上述 Bottom DEF 配套',
              '  run.log / time.log        # 原全局运行日志、计时记录',
              '  metrics.json              # 本例全局指标',
              '  h3d_report.json           # HBT/子网等完整结构化报告',
              '  command.json / hbt.json   # 原实际运行命令、资源配置',
              '  input_audit.json          # 输入来源与 LEF 路径', '```', '',
              'LEF 按原配置选择：aes、tinyRocket、ariane、bp 使用 `benchmarks_h3d/NangateOpenCellLibraryWithMacro.lef`；ethmac、mor1kx、jpegencode 使用 `benchmarks_h3d/NangateOpenCellLibraryWithoutMacro.lef`。各例路径见 input_audit.json。', '',
              '例如在项目根目录启动 OpenROAD 后，读取 aes Top 的对应输入：', '', '```tcl',
              'read_lef benchmarks_h3d/NangateOpenCellLibraryWithMacro.lef',
              'read_def results/aes/aes_top_hbt.place.def',
              'read_guides results/aes/aes_top.guide', '```', '',
              'Bottom 改用同目录的 `aes_btm_hbt.place.def` 与 `aes_btm.guide`。其他 case 按相同命名规则选择配套文件。', '',
              '`command.json` 保留原运行路径以供追溯；需要重新做全局布线时，应另选新的输出目录。', '',
              '根目录的 `global_routing_summary.csv/json` 是七例汇总，`global_routing_verification.json` 是独立核验记录，`publish_manifest.json` 记录全部副本的来源、大小和哈希。', '']
    with (destination / 'README.md').open('x') as stream:
        stream.write('\n'.join(lines))
    check_protected(experiment)
    print(json.dumps({'destination': str(destination), 'cases': list(CASES), 'def_guide_files': 4 * len(CASES),
                      'all_copies_hash_verified': True, 'matches_current_binary_and_source': True,
                      'nonzero_wire_overflow': {r['benchmark']: r['wire_overflow'] for r in rows if r['wire_overflow']}}, indent=2))


if __name__ == '__main__':
    main()
